/*
  ==============================================================================

    BatchRenderer.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "BatchRenderer.h"
#include "../Audio/MidiPlayer.h"

//==============================================================================
BatchRenderer::BatchRenderer(ayra::PluginsManager &pm, const RenderSettings &s,
                             const File &outputDir)
    : Thread("BatchRenderer"), pluginsManager(pm), settings(s),
      outputDirectory(outputDir) {}

BatchRenderer::~BatchRenderer() { cancelRendering(); }

//==============================================================================
void BatchRenderer::addJob(const RenderJob &job) {
  const ScopedLock sl(jobLock);
  pendingJobs.add(job);
  totalJobs = pendingJobs.size();
}

void BatchRenderer::startRendering() {
  if (pendingJobs.isEmpty())
    return;

  completedCount = 0;
  failedCount = 0;

  startThread();
}

void BatchRenderer::cancelRendering() {
  signalThreadShouldExit();
  waitForThreadToExit(5000);
}

//==============================================================================
float BatchRenderer::getProgress() const {
  if (totalJobs == 0)
    return 0.0f;

  return static_cast<float>(completedCount.load()) /
         static_cast<float>(totalJobs);
}

//==============================================================================
void BatchRenderer::run() {
  Array<RenderJob> jobsCopy;

  {
    const ScopedLock sl(jobLock);
    jobsCopy = pendingJobs;
  }

  // Process jobs sequentially
  for (auto &job : jobsCopy) {
    if (threadShouldExit())
      break;

    bool success = renderSingleJob(job);

    if (success) {
      completedCount++;
    } else {
      failedCount++;
    }

    triggerAsyncUpdate();
  }

  // Final update
  triggerAsyncUpdate();
}

void BatchRenderer::handleAsyncUpdate() {
  if (onProgress)
    onProgress(getProgress());

  if (isComplete() ||
      (completedCount.load() + failedCount.load() >= totalJobs)) {
    if (failedCount.load() > 0 && onError) {
      onError("Some renders failed: " + lastError);
    } else if (onComplete) {
      onComplete();
    }
  }
}

bool BatchRenderer::renderSingleJob(const RenderJob &job) {
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

    // Prepare plugin
    plugin->prepareToPlay(settings.sampleRate, 2048);

    // 2. Load MIDI and apply transformations
    auto midiSeq = MidiPlayer::loadMidiFile(job.midiFile, settings.bpm);
    midiSeq = MidiPlayer::applyTransformations(midiSeq, job.pitchOffset,
                                               job.velocityMultiplier);

    double midiDuration = MidiPlayer::getSequenceDuration(midiSeq);

    // Add extra time for synth tails (10 seconds)
    double renderDuration = midiDuration + 10.0;

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
      if (threadShouldExit()) {
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

    // 4. Apply volume gain
    if (job.volumeDb > -96.0f) {
      float volumeGain = Decibels::decibelsToGain(job.volumeDb);
      fullBuffer.applyGain(volumeGain);
    } else {
      // Mute (volumeDb <= -96 is treated as silence/mute)
      fullBuffer.clear();
    }

    // 5. Find silence start (below -50dB)
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

    // 5. Snap to next bar
    double silenceStartTime = silenceStartSample / settings.sampleRate;
    double barDuration = 60.0 / settings.bpm * 4.0; // 4 beats per bar
    double bars = silenceStartTime / barDuration;
    double nextBar = std::ceil(bars);
    double finalEndTime = nextBar * barDuration;

    // Ensure minimum duration (at least one bar)
    if (finalEndTime < barDuration)
      finalEndTime = barDuration;

    int64 finalSamples = static_cast<int64>(finalEndTime * settings.sampleRate);
    finalSamples = jmin(finalSamples, totalSamples);

    // 6. Write to file
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

    writer->writeFromAudioSampleBuffer(fullBuffer, 0,
                                       static_cast<int>(finalSamples));

    return true;
  } catch (const std::exception &e) {
    lastError = String("Exception: ") + e.what();
    return false;
  }
}
