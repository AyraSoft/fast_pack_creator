/*
  ==============================================================================

    ConfigurationPanel.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
// Playhead behavior modes for live preview
enum class PlayheadMode {
  Independent = 1, // Playhead cycles 0-16 bars continuously, plugins sync to it
  AtTrigger = 2,   // Playhead starts at 0 when triggered, stops after 4 bars
  NoMoving = 3     // Playhead always stays at 0
};

//==============================================================================
class ConfigurationPanel : public Component, private Timer {
public:
  //==============================================================================
  ConfigurationPanel();
  ~ConfigurationPanel() override = default;

  //==============================================================================
  void paint(Graphics &) override;
  void resized() override;

  //==============================================================================
  // Setters for programmatic updates
  void setNumVariations(int num);
  void setBpm(double bpm);
  void setPlayheadPosition(double ppqPosition); // Update the slider display

  // Getters
  bool isVariation1Enabled() const;
  double getVariation1Bpm() const;
  bool isVariation2Enabled() const;
  double getVariation2Bpm() const;
  bool isLoopEnabled() const { return loop.getToggleState(); }
  bool isSeamlessLoopEnabled() const { return seamlessLoop.getToggleState(); }
  bool isNormalizationEnabled() const {
    return applyNormalization.getToggleState();
  }
  double getNormalizationHeadroom() const {
    return normalizationHeadroom.getValue();
  }
  PlayheadMode getPlayheadMode() const {
    return static_cast<PlayheadMode>(progressType.getSelectedId());
  }
  double getCurrentBpm() const { return bpmSlider.getValue(); }

  //==============================================================================
  // Callbacks
  std::function<void(int numVariations)> onVariationsChanged;
  std::function<void(double bpm)> onBpmChanged;
  std::function<void(const File &midiFolder)> onMidiFolderSelected;
  std::function<void()> onMidiPanic;
  std::function<void(PlayheadMode mode)> onPlayheadModeChanged;
  std::function<void(double ppqPosition)>
      onPlayheadPositionChanged; // Sync independent position

  void reset();

  // Start/stop independent playhead cycling
  void startIndependentPlayhead();
  void stopIndependentPlayhead();
  bool isIndependentPlayheadRunning() const { return isTimerRunning(); }

private:
  //==============================================================================
  void timerCallback() override; // For independent mode cycling

  // Independent playhead state
  double independentPpqPosition = 0.0;
  double lastTimerCallbackTime = 0.0;

  Label variationsLabel{{}, "Variations:"};
  Slider variationsSlider;

  Label bpmLabel{{}, "BPM:"};
  Slider bpmSlider;

  ToggleButton variationBpm1{"BPM var1"};
  Slider variationBpm1Slider;
  ToggleButton variationBpm2{"BPM var2"};
  Slider variationBpm2Slider;

  ToggleButton loop{"Loop"};
  ToggleButton seamlessLoop{"Seamless"}; // Render MIDI twice, keep second half
  ToggleButton applyNormalization{"Normalize"};
  Slider normalizationHeadroom;

  ComboBox progressType{"Progress Type"};
  Slider progressPlayhead;

  TextButton selectFolderButton{"Select MIDI Folder"};
  Label folderPathLabel;

  std::unique_ptr<FileChooser> fileChooser;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigurationPanel)
};
