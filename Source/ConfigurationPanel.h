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
class ConfigurationPanel : public Component {
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
  // Getters
  bool isVariation1Enabled() const;
  double getVariation1Bpm() const;
  bool isVariation2Enabled() const;
  double getVariation2Bpm() const;

  //==============================================================================
  // Callbacks
  std::function<void(int numVariations)> onVariationsChanged;
  std::function<void(double bpm)> onBpmChanged;
  std::function<void(const File &midiFolder)> onMidiFolderSelected;
  std::function<void()> onMidiPanic;
    
   void reset();

private:
  //==============================================================================
  Label variationsLabel{{}, "Variations:"};
  Slider variationsSlider;

  Label bpmLabel{{}, "BPM:"};
  Slider bpmSlider;

  ToggleButton variationBpm1{"BPM var1"};
  Slider variationBpm1Slider;
  ToggleButton variationBpm2{"BPM var1"};
  Slider variationBpm2Slider;
    
    ToggleButton loop{"Loop"};

  TextButton selectFolderButton{"Select MIDI Folder"};
  Label folderPathLabel;

  std::unique_ptr<FileChooser> fileChooser;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ConfigurationPanel)
};
