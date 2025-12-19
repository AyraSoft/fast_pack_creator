/*
  ==============================================================================

    PluginHost.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class GainProcessor : public AudioProcessor {
public:
  GainProcessor()
      : AudioProcessor(
            BusesProperties()
                .withInput("Input", AudioChannelSet::stereo(), true)
                .withOutput("Output", AudioChannelSet::stereo(), true)) {}

  void prepareToPlay(double, int) override {}
  void releaseResources() override {}

  void processBlock(AudioBuffer<float> &buffer, MidiBuffer &) override {
    buffer.applyGain(gain);
  }

  void processBlock(AudioBuffer<double> &buffer, MidiBuffer &) override {
    buffer.applyGain(gain);
  }

  AudioProcessorEditor *createEditor() override { return nullptr; }
  bool hasEditor() const override { return false; }
  const String getName() const override { return "Gain"; }
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

  void setGain(float newGain) { gain = newGain; }

private:
  float gain = 1.0f;
};

//==============================================================================
class PluginHost : public AudioSource,
                   public MidiInputCallback,
                   public AudioPlayHead {
public:
  //==============================================================================
  PluginHost(AudioDeviceManager &dm, ayra::PluginsManager &pm);
  ~PluginHost() override;

  //==============================================================================
  // Plugin management
  // Uses ProxyProcessor internally to allow shared ownership with MidiGrid
  void setActivePlugin(AudioPluginInstance *plugin);
  AudioPluginInstance *getActivePlugin() const;

  void setAcceptingMidiInput(bool accept);
  bool isAcceptingMidiInput() const { return acceptMidiInput; }

  //==============================================================================
  // Preview playback
  void playMidiSequence(const MidiMessageSequence &seq, double bpm);
  void stopPlayback();
  bool isPlaying() const { return playing; }

  void setBpm(double newBpm);

  //==============================================================================
  // AudioPlayHead implementation
  juce::Optional<AudioPlayHead::PositionInfo> getPosition() const override;

  //==============================================================================
  // Audio level metering
  foleys::LevelMeterSource *getMeterSource() { return &meterSource; }

  void setGain(float gain);
  void setMasterGain(float gain);

private:
  //==============================================================================
  // AudioSource
  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void releaseResources() override;
  void getNextAudioBlock(const AudioSourceChannelInfo &bufferToFill) override;

  //==============================================================================
  // MidiInputCallback
  void handleIncomingMidiMessage(MidiInput *source,
                                 const MidiMessage &message) override;

private:
  //==============================================================================
  AudioDeviceManager &deviceManager;
  ayra::PluginsManager &pluginsManager;

  // Graph components
  std::unique_ptr<ayra::AudioProcessorGraph> graph;

  // Nodes
  ayra::AudioProcessorGraph::Node::Ptr midiInputNode;
  ayra::AudioProcessorGraph::Node::Ptr audioOutputNode;

  ayra::AudioProcessorGraph::Node::Ptr activePluginNode;
  ayra::AudioProcessorGraph::Node::Ptr activePluginGainNode;

  // Gain state for combined gain calculation
  float currentRowGain = 1.0f;
  float currentMasterGain = 1.0f;

  // State
  bool acceptMidiInput = false;
  std::atomic<bool> playing{false};

  // Playback state
  MidiMessageSequence playbackSequence;

  // Timing state
  std::atomic<double> currentBpm{120.0};
  double currentPpqPosition = 0.0;
  std::atomic<int64> currentSampleCount{0};
  int nextEventIndex = 0;

  // Audio processing
  double currentSampleRate = 44100.0;
  int currentBlockSize = 2048;

  MidiBuffer pendingMidiBuffer;
  CriticalSection midiLock;

  // Level metering
  foleys::LevelMeterSource meterSource;

  // Helpers
  void processMidiPlayback(MidiBuffer &midiBuffer, int numSamples);
  void rebuildGraph(AudioPluginInstance *plugin);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
