/*
  ==============================================================================

    ParallelBatchRenderer.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "ParallelBatchRenderer.h"
#include "../Audio/MidiPlayer.h"

//==============================================================================
// Custom playhead for offline rendering - provides tempo info to plugins (JUCE
// 8 API)
class OfflinePlayHead : public AudioPlayHead {
public:
  OfflinePlayHead(double bpmValue, double sr)
      : playheadBpm(bpmValue), playheadSampleRate(sr) {}

  Optional<PositionInfo> getPosition() const override {
    PositionInfo info;
    info.setBpm(playheadBpm);
    info.setTimeSignature(TimeSignature{4, 4});
    info.setTimeInSamples(currentSample);
    info.setTimeInSeconds(static_cast<double>(currentSample) /
                          playheadSampleRate);

    double timeInSec = static_cast<double>(currentSample) / playheadSampleRate;
    info.setPpqPosition((timeInSec / 60.0) * playheadBpm);
    info.setPpqPositionOfLastBarStart(0.0);
    info.setIsPlaying(true);
    info.setIsRecording(false);
    info.setIsLooping(false);
    return info;
  }

  void setPosition(int64 sample) { currentSample = sample; }

private:
  double playheadBpm;
  double playheadSampleRate;
  int64 currentSample = 0;
};

//==============================================================================
ParallelBatchRenderer::ParallelBatchRenderer(ayra::PluginsManager &pm,
                                             const RenderSettings &settings,
                                             const File &outputDir)
    : pluginsManager(pm), settings(settings), outputDirectory(outputDir) {
  // Prepare thread pool
  threadPool.prepare(settings.sampleRate, 2048);
}

ParallelBatchRenderer::~ParallelBatchRenderer() { cancelRendering(); }

//==============================================================================
void ParallelBatchRenderer::addJob(const RenderJob &job) {
  const ScopedLock sl(queueLock);

  // Find or create queue for this row
  RowQueue *queue = nullptr;
  for (auto *q : rowQueues) {
    if (q->rowIndex == job.rowIndex) {
      queue = q;
      break;
    }
  }

  if (queue == nullptr) {
    queue = new RowQueue();
    queue->rowIndex = job.rowIndex;
    rowQueues.add(queue);
  }

  queue->jobs.push(job);
  totalJobs++;
}

void ParallelBatchRenderer::startRendering() {
  if (rendering.load())
    return;

  const ScopedLock sl(queueLock);

  if (rowQueues.isEmpty())
    return;

  rendering.store(true);
  cancelled.store(false);
  completedCount.store(0);
  failedCount.store(0);

  // Start processing first job from each row in parallel
  for (auto *queue : rowQueues) {
    processNextJobForRow(queue->rowIndex);
  }

  // Start timer for progress updates
  startTimerHz(10);
}

void ParallelBatchRenderer::cancelRendering() {
  cancelled.store(true);
  stopTimer();
  rendering.store(false);

  const ScopedLock sl(queueLock);

  // Clear all queues
  for (auto *queue : rowQueues) {
    while (!queue->jobs.empty())
      queue->jobs.pop();
    queue->isProcessing.store(false);
  }
}

//==============================================================================
float ParallelBatchRenderer::getProgress() const {
  if (totalJobs == 0)
    return 1.0f;
  return static_cast<float>(completedCount.load()) /
         static_cast<float>(totalJobs);
}

//==============================================================================
void ParallelBatchRenderer::timerCallback() {
  if (onProgress)
    onProgress(getProgress());

  // Check if all complete
  if (completedCount.load() + failedCount.load() >= totalJobs) {
    stopTimer();
    rendering.store(false);

    if (failedCount.load() > 0 && onError) {
      onError("Some renders failed: " + lastError);
    } else if (onComplete) {
      onComplete();
    }
  }
}

void ParallelBatchRenderer::processNextJobForRow(int rowIndex) {
  if (cancelled.load())
    return;

  const ScopedLock sl(queueLock);

  // Find the queue for this row
  RowQueue *queue = nullptr;
  for (auto *q : rowQueues) {
    if (q->rowIndex == rowIndex) {
      queue = q;
      break;
    }
  }

  if (queue == nullptr || queue->jobs.empty()) {
    if (queue)
      queue->isProcessing.store(false);
    return;
  }

  // Check if already processing
  if (queue->isProcessing.exchange(true))
    return;

  // Get next job
  RenderJob job = queue->jobs.front();
  queue->jobs.pop();

  // Submit to thread pool
  threadPool.addJob([this, job, rowIndex]() {
    bool success = renderSingleJob(job);
    onJobCompleted(rowIndex, success, lastError);
  });
}

void ParallelBatchRenderer::onJobCompleted(int rowIndex, bool success,
                                           const String &error) {
  if (success) {
    completedCount++;
  } else {
    failedCount++;
    if (!error.isEmpty())
      lastError = error;
  }

  // Find queue and mark as not processing
  {
    const ScopedLock sl(queueLock);
    for (auto *queue : rowQueues) {
      if (queue->rowIndex == rowIndex) {
        queue->isProcessing.store(false);
        break;
      }
    }
  }

  // Process next job for this row (sequential per row)
  if (!cancelled.load()) {
    processNextJobForRow(rowIndex);
  }
}

//==============================================================================
bool ParallelBatchRenderer::renderSingleJob(const RenderJob &job) {
  try {
    // 1. Load plugin
    String errorMessage;
    ayra::PluginDescriptionAndPreference descPref;
    descPref.pluginDescription = job.pluginDesc;

    auto plugin = pluginsManager.createPluginInstance(
        descPref, settings.sampleRate, 2048, errorMessage);

    if (plugin == nullptr) {
      lastError = "Failed to load plugin: " + errorMessage;
      return false;
    }

    // Restore plugin state
    if (job.pluginState.getSize() > 0) {
      plugin->setStateInformation(job.pluginState.getData(),
                                  static_cast<int>(job.pluginState.getSize()));
    }

    // Create playhead with job's BPM for tempo-synced plugins (arpeggiators,
    // etc.)
    OfflinePlayHead playhead(job.bpm, settings.sampleRate);
    plugin->setPlayHead(&playhead);

    // Prepare plugin
    plugin->prepareToPlay(settings.sampleRate, 2048);

    // 2. Load MIDI and apply transformations (use job.bpm, not settings.bpm)
    auto midiSeq = MidiPlayer::loadMidiFile(job.midiFile, job.bpm);
    midiSeq = MidiPlayer::applyTransformations(midiSeq, job.pitchOffset,
                                               job.velocityMultiplier);

    double midiDuration = MidiPlayer::getSequenceDuration(midiSeq);
    // Use getMidiFileDuration for loop duration - it rounds to complete bars
    double originalMidiDuration =
        MidiPlayer::getMidiFileDuration(job.midiFile, job.bpm);

    // For seamless loops: duplicate MIDI (play it twice) so second half has
    // tail from first
    bool doSeamlessLoop = settings.loop && settings.seamlessLoop;
    if (doSeamlessLoop) {
      // Create duplicated MIDI events offset by originalMidiDuration (full
      // bar-rounded duration)
      int numEvents = midiSeq.getNumEvents();
      for (int i = 0; i < numEvents; ++i) {
        auto *event = midiSeq.getEventPointer(i);
        MidiMessage newMsg = event->message;
        newMsg.setTimeStamp(newMsg.getTimeStamp() + originalMidiDuration);
        midiSeq.addEvent(newMsg);
      }
      midiSeq.sort();
      DBG("Seamless loop: duplicated MIDI with offset = " +
          String(originalMidiDuration) +
          "s, total duration = " + String(originalMidiDuration * 2) + "s");
    }

    // Calculate render duration
    double renderDuration = doSeamlessLoop ? (originalMidiDuration * 2 + 5.0)
                                           : (midiDuration + 10.0);

    // 3. Render through plugin
    int64 totalSamples =
        static_cast<int64>(renderDuration * settings.sampleRate);
    int numChannels = plugin->getTotalNumOutputChannels();
    if (numChannels < 2)
      numChannels = 2;

    AudioBuffer<float> fullBuffer(numChannels, static_cast<int>(totalSamples));
    fullBuffer.clear();

    const int blockSize = 2048;
    int64 samplePos = 0;
    int midiEventIndex = 0;

    while (samplePos < totalSamples) {
      if (cancelled.load()) {
        plugin->releaseResources();
        return false;
      }

      int samplesToProcess =
          static_cast<int>(jmin((int64)blockSize, totalSamples - samplePos));

      // Create temp buffer for this block
      AudioBuffer<float> blockBuffer(numChannels, samplesToProcess);
      blockBuffer.clear();

      // Create MIDI buffer for this block
      MidiBuffer midiBuffer;

      double blockStartTime = samplePos / settings.sampleRate;
      double blockEndTime =
          (samplePos + samplesToProcess) / settings.sampleRate;

      // Add MIDI events that fall within this block
      while (midiEventIndex < midiSeq.getNumEvents()) {
        auto *event = midiSeq.getEventPointer(midiEventIndex);
        double eventTime = event->message.getTimeStamp();

        if (eventTime < blockStartTime) {
          midiEventIndex++;
          continue;
        }

        if (eventTime < blockEndTime) {
          int sampleOffset = static_cast<int>((eventTime - blockStartTime) *
                                              settings.sampleRate);
          sampleOffset = jlimit(0, samplesToProcess - 1, sampleOffset);
          midiBuffer.addEvent(event->message, sampleOffset);
          midiEventIndex++;
        } else {
          break;
        }
      }

      // Update playhead position for tempo-synced plugins
      playhead.setPosition(samplePos);

      // Process block through plugin
      plugin->processBlock(blockBuffer, midiBuffer);

      // Copy to full buffer
      for (int ch = 0; ch < numChannels; ++ch) {
        fullBuffer.copyFrom(ch, static_cast<int>(samplePos), blockBuffer, ch, 0,
                            samplesToProcess);
      }

      samplePos += samplesToProcess;
    }

    plugin->releaseResources();

    // 4. Apply combined volume gain (row gain + master gain)
    float rowGainLinear =
        (job.volumeDb > -96.0f) ? Decibels::decibelsToGain(job.volumeDb) : 0.0f;
    float masterGainLinear =
        (settings.masterGainDb > -96.0f)
            ? Decibels::decibelsToGain(settings.masterGainDb)
            : 0.0f;
    float combinedGain = rowGainLinear * masterGainLinear;

    if (combinedGain > 0.0f) {
      fullBuffer.applyGain(combinedGain);
    } else {
      fullBuffer.clear();
    }

    // 5. Determine final sample count based on loop mode
    int64 finalSamples;
    int64 startSampleOffset = 0; // For seamless loop, we skip the first half

    if (settings.loop) {
      // Loop mode: truncate exactly at MIDI duration for seamless looping
      // Use originalMidiDuration which is the single loop duration
      finalSamples =
          static_cast<int64>(originalMidiDuration * settings.sampleRate);

      // For seamless loop: skip the first half (first loop iteration without
      // tail) and keep only the second half (which has the tail from the first
      // iteration)
      if (doSeamlessLoop) {
        startSampleOffset =
            static_cast<int64>(originalMidiDuration * settings.sampleRate);
        DBG("Seamless loop: keeping samples from " + String(startSampleOffset) +
            " to " + String(startSampleOffset + finalSamples) +
            " (original duration: " + String(originalMidiDuration) + "s)");
      }
    } else {
      // Normal mode: find silence start and snap to next bar
      float thresholdLinear =
          Decibels::decibelsToGain(settings.silenceThresholdDb);
      int64 silenceStartSample = totalSamples;

      // Scan backwards
      for (int64 pos = totalSamples - blockSize; pos >= 0; pos -= blockSize) {
        int samplesToCheck =
            static_cast<int>(jmin((int64)blockSize, totalSamples - pos));

        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch) {
          peak = jmax(peak, fullBuffer.getMagnitude(ch, static_cast<int>(pos),
                                                    samplesToCheck));
        }

        if (peak > thresholdLinear) {
          silenceStartSample = pos + samplesToCheck;
          break;
        }
      }

      // Snap to next bar
      double silenceStartTime = silenceStartSample / settings.sampleRate;
      double barDuration = 60.0 / settings.bpm * 4.0; // 4 beats per bar
      double bars = silenceStartTime / barDuration;
      double nextBar = std::ceil(bars);
      double finalEndTime = nextBar * barDuration;

      // Ensure minimum duration (at least one bar)
      if (finalEndTime < barDuration)
        finalEndTime = barDuration;

      finalSamples = static_cast<int64>(finalEndTime * settings.sampleRate);
    }

    finalSamples = jmin(finalSamples, totalSamples);

    // 7. Write to file
    job.outputFile.getParentDirectory().createDirectory();
    job.outputFile.deleteFile();

    std::unique_ptr<FileOutputStream> outputStream(
        job.outputFile.createOutputStream());
    if (outputStream == nullptr) {
      lastError = "Failed to create output file";
      return false;
    }

    WavAudioFormat wavFormat;
    std::unique_ptr<AudioFormatWriter> writer(wavFormat.createWriterFor(
        outputStream.get(), settings.sampleRate,
        static_cast<unsigned int>(numChannels), settings.bitDepth, {}, 0));

    if (writer == nullptr) {
      lastError = "Failed to create WAV writer";
      return false;
    }

    outputStream.release(); // Writer now owns the stream

    writer->writeFromAudioSampleBuffer(fullBuffer,
                                       static_cast<int>(startSampleOffset),
                                       static_cast<int>(finalSamples));

    // Close writer before normalizing
    writer.reset();

    // 8. Normalization is now done as batch post-processing after all renders
    // complete (see MainComponent::runBatchNormalization)

    // 9. Validate output file - check for empty or silent files
    if (job.outputFile.existsAsFile()) {
      int64 fileSize = job.outputFile.getSize();

      // Check if file is empty or too small (< 1KB is suspicious for audio)
      if (fileSize < 1024) {
        ScopedLock sl(problemFilesLock);
        problematicFiles.add(job.outputFile.getFileName() +
                             " [EMPTY/CORRUPT - " + String(fileSize) +
                             " bytes]");
        DBG("WARNING: File too small: " + job.outputFile.getFileName());
      } else {
        // Check for silent audio by reading and checking peak level
        WavAudioFormat wavFormat;
        std::unique_ptr<AudioFormatReader> reader(wavFormat.createReaderFor(
            job.outputFile.createInputStream().release(), true));

        if (reader != nullptr) {
          // Read a sample of the audio to check levels
          AudioBuffer<float> checkBuffer(
              reader->numChannels,
              jmin((int)reader->lengthInSamples, 44100)); // Check first 1 sec
          reader->read(&checkBuffer, 0, checkBuffer.getNumSamples(), 0, true,
                       true);

          float peakLevel =
              checkBuffer.getMagnitude(0, checkBuffer.getNumSamples());

          // If peak is below -60dB, file is essentially silent
          if (peakLevel < 0.001f) { // ~-60dB
            ScopedLock sl(problemFilesLock);
            problematicFiles.add(
                job.outputFile.getFileName() + " [SILENT - peak: " +
                String(20.0f * std::log10(peakLevel + 0.0001f), 1) + " dB]");
            DBG("WARNING: Silent file: " + job.outputFile.getFileName());
          }
        }
      }
    } else {
      ScopedLock sl(problemFilesLock);
      problematicFiles.add(job.outputFile.getFileName() +
                           " [FILE NOT CREATED]");
    }

    return true;
  } catch (const std::exception &e) {
    lastError = String("Exception: ") + e.what();
    return false;
  }
}
