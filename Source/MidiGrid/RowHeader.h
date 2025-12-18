/*
  ==============================================================================

    RowHeader.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class RowHeader : public Component,
                  public Button::Listener,
                  public Slider::Listener {
public:
  //==============================================================================
  RowHeader(int variationIndex, ayra::PluginsManager &pm);
  ~RowHeader() override;

  //==============================================================================
  void paint(Graphics &) override;
  void resized() override;
  void mouseDown(const MouseEvent &e) override;
  void mouseDoubleClick(const MouseEvent &e) override;

  //==============================================================================
  void setSelected(bool selected);
  bool isSelected() const { return selected; }

  AudioPluginInstance *getPlugin() const { return plugin.get(); }
  PluginDescription getPluginDescription() const { return pluginDesc; }
  MemoryBlock getPluginState() const;
  String getVariationName() const { return nameLabel.getText(); }

  // Load plugin programmatically (for macro operations)
  void setPlugin(std::unique_ptr<AudioPluginInstance> newPlugin,
                 const PluginDescription &desc);

  //==============================================================================
  // Volume control (-96dB as mute, to +12dB)
  float getVolumeDb() const { return volumeDb; }
  void setVolumeDb(float db);

  //==============================================================================
  std::function<void()> onSelected;
  std::function<void()> onPluginLoaded;
  std::function<void()>
      onMacroToggle; // Called when macro toggle button clicked
  std::function<void(float)> onVolumeChanged;

  //==============================================================================
  // Button::Listener
  void buttonClicked(Button *) override;

  // Slider::Listener
  void sliderValueChanged(Slider *slider) override;

  bool isPluginEditorShown() const {
    return pluginEditorWindow.get() ? true : false;
  }

  void showPluginEditor();
  void closePluginEditor() {
    if (pluginEditorWindow.get()) {
      pluginEditorWindow.reset();
    }
  }

private:
  //==============================================================================
  int index;
  bool selected = false;
  float volumeDb = 0.0f;

  ayra::PluginsManager &pluginsManager;

  Label nameLabel;
  TextButton loadPluginButton{"Load"};
  TextButton editPluginButton{"Edit"};
  TextButton removePluginButton{"X"};
  Label pluginNameLabel;

  std::unique_ptr<DocumentWindow> pluginEditorWindow;

  // Volume control
  Slider volumeSlider;
  //  Label volumeLabel;

  TextButton macroToggle{""}; // 30x30 toggle button at left

  PluginDescription pluginDesc;
  std::unique_ptr<AudioPluginInstance> plugin;

  //==============================================================================
  void showPluginMenu();
  void loadPlugin(const ayra::PluginDescriptionAndPreference &desc);
  void removePlugin();

  //  void updateVolumeLabel();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RowHeader)
};
