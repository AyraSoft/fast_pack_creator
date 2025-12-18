/*
  ==============================================================================

    PluginHost.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "PluginHost.h"

//==============================================================================
PluginHost::PluginHost(AudioDeviceManager &dm, ayra::PluginsManager &pm)
    : deviceManager(dm), pluginsManager(pm) {
  // Register as MIDI input callback
  auto midiInputs = MidiInput::getAvailableDevices();
  for (auto &input : midiInputs) {
    deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
    deviceManager.addMidiInputDeviceCallback(input.identifier, this);
  }
}

PluginHost::~PluginHost() {

  auto midiInputs = MidiInput::getAvailableDevices();
  for (auto &input : midiInputs) {
    deviceManager.removeMidiInputDeviceCallback(input.identifier, this);
  }
}

//==============================================================================
void PluginHost::setActivePlugin(AudioPluginInstance *plugin) {
  const ScopedLock sl(midiLock);

  activePlugin = plugin;

  if (activePlugin != nullptr) {
    activePlugin->prepareToPlay(currentSampleRate, currentBlockSize);
    activePlugin->setPlayHead(this);
  }
}

void PluginHost::setAcceptingMidiInput(bool accept) {
  acceptMidiInput = accept;
}

//==============================================================================
void PluginHost::playMidiSequence(const MidiMessageSequence &seq, double bpm) {
  const ScopedLock sl(midiLock);

  playbackSequence = seq;
  currentBpm = bpm;
  currentPpqPosition = 0.0;
  currentSampleCount = 0;
  nextEventIndex = 0;
  playing = true;
}

void PluginHost::stopPlayback() {
  const ScopedLock sl(midiLock);

  playing = false;

  // Send all notes off
  if (activePlugin != nullptr) {
    MidiBuffer allNotesOff;
    for (int ch = 1; ch <= 16; ++ch) {
      allNotesOff.addEvent(MidiMessage::allNotesOff(ch), 0);
    }

    AudioBuffer<float> dummyBuffer(2, currentBlockSize);
    dummyBuffer.clear();
    activePlugin->processBlock(dummyBuffer, allNotesOff);
  }
}

void PluginHost::setBpm(double newBpm) { currentBpm = newBpm; }

//==============================================================================
Optional<AudioPlayHead::PositionInfo> PluginHost::getPosition() const {
  AudioPlayHead::PositionInfo info;

  info.setBpm(currentBpm);
  AudioPlayHead::TimeSignature ts;
  ts.numerator = 4;
  ts.denominator = 4;
  info.setTimeSignature(ts);
  info.setTimeInSamples(currentSampleCount);
  info.setTimeInSeconds(static_cast<double>(currentSampleCount) /
                        (currentSampleRate > 0 ? currentSampleRate : 44100.0));

  info.setPpqPosition(currentPpqPosition);
  info.setPpqPositionOfLastBarStart(std::floor(currentPpqPosition / 4.0) * 4.0);

  info.setIsPlaying(playing);
  info.setIsLooping(false);
  info.setIsRecording(false);

  return info;
}

//==============================================================================
void PluginHost::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
  currentSampleRate = sampleRate;
  currentBlockSize = samplesPerBlockExpected;

  // Initialize level meter source (2 channels, ~50ms RMS window)
  int rmsWindow = static_cast<int>(sampleRate * 0.05 / samplesPerBlockExpected);
  meterSource.resize(2, rmsWindow > 0 ? rmsWindow : 8);

  if (activePlugin != nullptr) {
    activePlugin->prepareToPlay(sampleRate, samplesPerBlockExpected);
  }
}

void PluginHost::releaseResources() {
  if (activePlugin != nullptr) {
    activePlugin->releaseResources();
  }
}

void PluginHost::getNextAudioBlock(const AudioSourceChannelInfo &bufferToFill) {
  bufferToFill.clearActiveBufferRegion();

  if (activePlugin == nullptr)
    return;

  MidiBuffer midiBuffer;
  int numSamples = bufferToFill.numSamples;

  {
    const ScopedLock sl(midiLock);

    // Add any pending MIDI from input
    midiBuffer.swapWith(pendingMidiBuffer);

    // Logic for playback
    if (playing) {
      processMidiPlayback(midiBuffer, numSamples);

      // Advance time
      double bpm = currentBpm;
      double beatsPerSecond = bpm / 60.0;
      double samplesPerBeat = currentSampleRate / beatsPerSecond;
      double beatsInBlock = numSamples / samplesPerBeat;

      currentPpqPosition += beatsInBlock;
      currentSampleCount += numSamples;

      // Check end of sequence (assuming last event time + 1 beat margin)
      if (playbackSequence.getNumEvents() > 0) {
        if (currentPpqPosition >
            playbackSequence.getEndTime() + 4.0) { // +1 bar release
          // optionally handle auto-stop
        }
      }
    }
  }

  // Se playhead is set on plugin (done in setActivePlugin), we are good.

  // Process audio through plugin
  AudioBuffer<float> buffer(bufferToFill.buffer->getArrayOfWritePointers(),
                            bufferToFill.buffer->getNumChannels(),
                            bufferToFill.startSample, bufferToFill.numSamples);

  activePlugin->processBlock(buffer, midiBuffer);

  // Apply gain
  float combinedGain = outputGain * masterGain;
  if (combinedGain != 1.0f)
    buffer.applyGain(combinedGain);

  // Measure audio levels for meter (post-gain)
  meterSource.measureBlock(buffer);
}

//==============================================================================
void PluginHost::handleIncomingMidiMessage(MidiInput *source,
                                           const MidiMessage &message) {
  ignoreUnused(source);

  if (!acceptMidiInput)
    return;

  const ScopedLock sl(midiLock);
  pendingMidiBuffer.addEvent(message, 0);
}

void PluginHost::processMidiPlayback(MidiBuffer &midiBuffer, int numSamples) {
  if (!playing || playbackSequence.getNumEvents() == 0)
    return;

  double bpm = currentBpm;
  double beatsPerSecond = bpm / 60.0;
  double samplesPerBeat = currentSampleRate / beatsPerSecond;

  // Range of PPQ for this block
  double startPpq = currentPpqPosition;
  double endPpq = startPpq + (numSamples / samplesPerBeat);

  // Find events in this PPQ range
  while (nextEventIndex < playbackSequence.getNumEvents()) {
    auto *event = playbackSequence.getEventPointer(nextEventIndex);
    // Timestamps are now expected to be in PPQ (Beats)
    double eventPpq = event->message.getTimeStamp();

    if (eventPpq < startPpq) {
      // Catch up missed events at start of block
      midiBuffer.addEvent(event->message, 0);
      nextEventIndex++;
    } else if (eventPpq < endPpq) {
      // Event falls in this block
      double ppqOffset = eventPpq - startPpq;
      int sampleOffset = static_cast<int>(ppqOffset * samplesPerBeat);
      sampleOffset = jlimit(0, numSamples - 1, sampleOffset);

      midiBuffer.addEvent(event->message, sampleOffset);
      nextEventIndex++;
    } else {
      break;
    }
  }
}
