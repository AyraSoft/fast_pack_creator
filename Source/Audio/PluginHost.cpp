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

  // Initialize Graph
  graph = std::make_unique<ayra::AudioProcessorGraph>();
  graph->setProcessingMode(ayra::ProcessingMode::multiThread);

  // Create fixed nodes
  // MIDI Input Node
  auto midiInputProcessor =
      std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
          AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode);
  midiInputNode = graph->addNode(std::move(midiInputProcessor));

  // Audio Output Node
  auto audioOutputProcessor =
      std::make_unique<AudioProcessorGraph::AudioGraphIOProcessor>(
          AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode);
  audioOutputNode = graph->addNode(std::move(audioOutputProcessor));

  // Register as MIDI input callback
  auto midiInputs = MidiInput::getAvailableDevices();
  for (auto &input : midiInputs) {
    deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
    deviceManager.addMidiInputDeviceCallback(input.identifier, this);
  }

  acceptMidiInput = true; // Enable MIDI input by default
}

PluginHost::~PluginHost() {

  auto midiInputs = MidiInput::getAvailableDevices();
  for (auto &input : midiInputs) {
    deviceManager.removeMidiInputDeviceCallback(input.identifier, this);
  }
}

//==============================================================================
//==============================================================================
void PluginHost::setActivePlugin(AudioPluginInstance *plugin) {
  const ScopedLock sl(midiLock);

  // Rebuild graph topology
  rebuildGraph(std::move(plugin));
}

// Proxy Processor to wrap external plugins in the graph without taking
// ownership
class ProxyProcessor : public AudioProcessor {
public:
  ProxyProcessor(AudioPluginInstance *target)
      : AudioProcessor(
            BusesProperties()
                .withInput("Input", AudioChannelSet::stereo(), true)
                .withOutput("Output", AudioChannelSet::stereo(), true)),
        targetProcessor(target) {}

  void prepareToPlay(double sampleRate, int samplesPerBlock) override {
    if (targetProcessor) {
      targetProcessor->enableAllBuses();
      targetProcessor->prepareToPlay(sampleRate, samplesPerBlock);
    }
  }

  void releaseResources() override {
    // We do NOT release resources of the target here, as we don't own it.
    // The owner (MidiGrid) manages the lifecycle.
  }

  void processBlock(AudioBuffer<float> &buffer,
                    MidiBuffer &midiMessages) override {
    if (targetProcessor) {
      targetProcessor->processBlock(buffer, midiMessages);
    }
  }

  void processBlock(AudioBuffer<double> &buffer,
                    MidiBuffer &midiMessages) override {
    if (targetProcessor)
      targetProcessor->processBlock(buffer, midiMessages);
  }

  AudioProcessorEditor *createEditor() override { return nullptr; }
  bool hasEditor() const override { return false; }
  const String getName() const override { return "Proxy"; }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return true; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const String getProgramName(int) override { return "Default"; }
  void changeProgramName(int, const String &) override {}
  void getStateInformation(MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

  AudioPluginInstance *getTarget() const { return targetProcessor; }

private:
  AudioPluginInstance *targetProcessor = nullptr;
};

void PluginHost::rebuildGraph(AudioPluginInstance *plugin) {
  // Remove old plugin and gain nodes if they exist
  if (activePluginNode != nullptr) {
    graph->removeNode(activePluginNode.get());
    activePluginNode = nullptr;
  }
  if (activePluginGainNode != nullptr) {
    graph->removeNode(activePluginGainNode.get());
    activePluginGainNode = nullptr;
  }

  if (plugin != nullptr) {
    // prepare plugin
    plugin->enableAllBuses();
    plugin->setPlayHead(this);

    // Add plugin node VIA PROXY
    auto proxy = std::make_unique<ProxyProcessor>(plugin);
    activePluginNode = graph->addNode(std::move(proxy));

    // Add plugin gain node (applies combined rowGain * masterGain)
    auto gainProc = std::make_unique<GainProcessor>();
    gainProc->setGain(currentRowGain * currentMasterGain);
    activePluginGainNode = graph->addNode(std::move(gainProc));

    // Connect: Midi Input -> Plugin (Proxy)
    graph->addConnection(
        {{midiInputNode->nodeID, ayra::AudioProcessorGraph::midiChannel},
         {activePluginNode->nodeID, ayra::AudioProcessorGraph::midiChannel}});

    // Connect: Plugin (Proxy) -> Plugin Gain
    graph->addConnection(
        {{activePluginNode->nodeID, 0}, {activePluginGainNode->nodeID, 0}});
    graph->addConnection(
        {{activePluginNode->nodeID, 1}, {activePluginGainNode->nodeID, 1}});

    // Connect: Plugin Gain -> Audio Output (direct connection, no master gain
    // node)
    graph->addConnection(
        {{activePluginGainNode->nodeID, 0}, {audioOutputNode->nodeID, 0}});
    graph->addConnection(
        {{activePluginGainNode->nodeID, 1}, {audioOutputNode->nodeID, 1}});

    // Rebuild the graph's internal processing order after topology changes
    graph->rebuild();

    // Re-prepare the graph with current sample rate and block size
    if (currentSampleRate > 0 && currentBlockSize > 0) {
      graph->prepareToPlay(currentSampleRate, currentBlockSize);
    }
  }
}

AudioPluginInstance *PluginHost::getActivePlugin() const {
  if (activePluginNode) {
    if (auto *proxy =
            dynamic_cast<ProxyProcessor *>(activePluginNode->getProcessor())) {
      return proxy->getTarget();
    }
  }
  return nullptr;
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

  // For AtTrigger mode: reset trigger position when starting playback
  if (playheadMode == PlayheadMode::AtTrigger) {
    triggerPpqPosition = 0.0;
    triggerActive = true;
  }
}

void PluginHost::stopPlayback() {
  const ScopedLock sl(midiLock);

  playing = false;
  triggerActive = false;

  // Send all notes off
  if (activePluginNode != nullptr) {
    MidiBuffer allNotesOff;
    for (int ch = 1; ch <= 16; ++ch) {
      allNotesOff.addEvent(MidiMessage::allNotesOff(ch), 0);
    }

    if (auto *proc = activePluginNode->getProcessor()) {
      AudioBuffer<float> dummyBuffer(2, currentBlockSize);
      dummyBuffer.clear();
      proc->processBlock(dummyBuffer, allNotesOff);
    }
  }
}

void PluginHost::setBpm(double newBpm) { currentBpm = newBpm; }

//==============================================================================
void PluginHost::setPlayheadMode(PlayheadMode mode) {
  playheadMode = mode;

  // Reset trigger state when changing mode
  if (mode != PlayheadMode::AtTrigger) {
    triggerActive = false;
  }
}

void PluginHost::setIndependentPlayheadPosition(double ppqPosition) {
  independentPpqPosition.store(ppqPosition);
}

//==============================================================================
Optional<AudioPlayHead::PositionInfo> PluginHost::getPosition() const {
  AudioPlayHead::PositionInfo info;

  info.setBpm(currentBpm);
  AudioPlayHead::TimeSignature ts;
  ts.numerator = 4;
  ts.denominator = 4;
  info.setTimeSignature(ts);

  // Determine PPQ position based on playhead mode
  double ppqPos = 0.0;
  bool isCurrentlyPlaying = false;

  switch (playheadMode) {
  case PlayheadMode::Independent:
    // Use the global independent playhead position (cycles 0-64 PPQ = 16 bars)
    ppqPos = independentPpqPosition.load();
    isCurrentlyPlaying =
        true; // Always considered "playing" in independent mode
    break;

  case PlayheadMode::AtTrigger:
    // Use trigger position if active, otherwise 0
    if (triggerActive) {
      ppqPos = triggerPpqPosition;
      isCurrentlyPlaying = true;
    } else {
      ppqPos = 0.0;
      isCurrentlyPlaying = false;
    }
    break;

  case PlayheadMode::NoMoving:
    // Always return 0
    ppqPos = 0.0;
    isCurrentlyPlaying = playing.load();
    break;
  }

  info.setTimeInSamples(currentSampleCount);
  info.setTimeInSeconds(static_cast<double>(currentSampleCount) /
                        (currentSampleRate > 0 ? currentSampleRate : 44100.0));

  info.setPpqPosition(ppqPos);
  info.setPpqPositionOfLastBarStart(std::floor(ppqPos / 4.0) * 4.0);

  info.setIsPlaying(isCurrentlyPlaying);
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

  // Set IO channels for the graph (stereo in/out)
  graph->setPlayConfigDetails(0, 2, sampleRate, samplesPerBlockExpected);
  graph->setPlayHead(this);
  graph->prepareToPlay(sampleRate, samplesPerBlockExpected);
}

void PluginHost::releaseResources() { graph->releaseResources(); }

void PluginHost::getNextAudioBlock(const AudioSourceChannelInfo &bufferToFill) {
  bufferToFill.clearActiveBufferRegion();

  MidiBuffer midiBuffer;
  int numSamples = bufferToFill.numSamples;

  {
    const ScopedLock sl(midiLock);

    // Add any pending MIDI from input
    midiBuffer.swapWith(pendingMidiBuffer);

    // Logic for playback
    if (playing) {
      processMidiPlayback(midiBuffer, numSamples);

      // Advance time for MIDI playback (always advances for MIDI event timing)
      double bpm = currentBpm;
      double beatsPerSecond = bpm / 60.0;
      double samplesPerBeat = currentSampleRate / beatsPerSecond;
      double beatsInBlock = numSamples / samplesPerBeat;

      currentPpqPosition += beatsInBlock;
      currentSampleCount += numSamples;

      // For AtTrigger mode: advance trigger position and check 4-bar limit
      if (playheadMode == PlayheadMode::AtTrigger && triggerActive) {
        triggerPpqPosition += beatsInBlock;

        // Stop after 4 bars (16 beats in 4/4)
        if (triggerPpqPosition >= 16.0) {
          triggerActive = false;
          triggerPpqPosition = 0.0;
        }
      }
    }
  }

  // Process audio through graph
  AudioBuffer<float> buffer(bufferToFill.buffer->getArrayOfWritePointers(),
                            bufferToFill.buffer->getNumChannels(),
                            bufferToFill.startSample, bufferToFill.numSamples);

  // Process audio through the graph
  graph->processBlock(buffer, midiBuffer);

  // Measure audio levels for meter (post-gain)
  meterSource.measureBlock(buffer);
}

void PluginHost::setGain(float gain) {
  currentRowGain = gain;
  // Apply combined gain
  if (activePluginGainNode) {
    if (auto *proc = dynamic_cast<GainProcessor *>(
            activePluginGainNode->getProcessor())) {
      proc->setGain(currentRowGain * currentMasterGain);
    }
  }
}

void PluginHost::setMasterGain(float gain) {
  currentMasterGain = gain;
  // Apply combined gain to active plugin
  if (activePluginGainNode) {
    if (auto *proc = dynamic_cast<GainProcessor *>(
            activePluginGainNode->getProcessor())) {
      proc->setGain(currentRowGain * currentMasterGain);
    }
  }
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
