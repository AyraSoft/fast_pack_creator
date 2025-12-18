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
#include <ff_meters/ff_meters.h>

//==============================================================================
class PluginHost : public AudioSource,
                   public MidiInputCallback,
                   public AudioPlayHead {
public:
  //==============================================================================
  PluginHost(AudioDeviceManager &dm, ayra::PluginsManager &pm);
  ~PluginHost() override;

  //==============================================================================
  void setActivePlugin(AudioPluginInstance *plugin);
  AudioPluginInstance *getActivePlugin() const { return activePlugin; }

  void setAcceptingMidiInput(bool accept);
  bool isAcceptingMidiInput() const { return acceptMidiInput; }

  //==============================================================================
  // Preview playback
  // Sequence timestamps must be in BEATS (PPQ) not seconds!
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

  void setGain(float gain) { outputGain = gain; }
  void setMasterGain(float gain) { masterGain = gain; }

private:
  float outputGain = 1.0f;
  float masterGain = 1.0f;

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

  AudioPluginInstance *activePlugin = nullptr;

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
  int currentBlockSize = 512;

  MidiBuffer pendingMidiBuffer;
  CriticalSection midiLock;

  // Level metering
  foleys::LevelMeterSource meterSource;

  //==============================================================================
  void processMidiPlayback(MidiBuffer &midiBuffer, int numSamples);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginHost)
};
