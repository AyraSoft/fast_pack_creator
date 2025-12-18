/*
  ==============================================================================

    RowHeader.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "RowHeader.h"

//==============================================================================
RowHeader::RowHeader(int variationIndex, ayra::PluginsManager &pm)
    : index(variationIndex), pluginsManager(pm) {
  // Name label (editable)
  nameLabel.setText("Var " + String(index + 1), dontSendNotification);
  nameLabel.setEditable(true);
  nameLabel.setJustificationType(Justification::centredLeft);
  addAndMakeVisible(nameLabel);

  // Load plugin button
  loadPluginButton.addListener(this);
  addAndMakeVisible(loadPluginButton);

  // Edit plugin button (opens GUI)
  editPluginButton.addListener(this);
  editPluginButton.setEnabled(false);
  addAndMakeVisible(editPluginButton);

  // Remove plugin button
  removePluginButton.addListener(this);
  removePluginButton.setEnabled(false);
  addAndMakeVisible(removePluginButton);

  macroToggle.addListener(this);
  addAndMakeVisible(macroToggle);
  macroToggle.onClick = [this] {
    if (onMacroToggle)
      onMacroToggle();
  };

  // Plugin name label
  pluginNameLabel.setText("No plugin", dontSendNotification);
  pluginNameLabel.setColour(Label::textColourId, Colours::grey);
  pluginNameLabel.setFont(Font(11.0f));
  addAndMakeVisible(pluginNameLabel);

  // Volume slider (-96dB to +12dB, using -96 as practical "mute")
  volumeSlider.setSliderStyle(Slider::LinearHorizontal);
  //  volumeSlider.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
  volumeSlider.setRange(-96.0, 12.0, 0.1);
  volumeSlider.setValue(0.0, dontSendNotification);

  volumeSlider.setSkewFactorFromMidPoint(
      -12.0); // Make -12dB the mid point for better control
  volumeSlider.addListener(this);
  addAndMakeVisible(volumeSlider);

  // Volume label
  //  volumeLabel.setJustificationType(Justification::centred);
  //  volumeLabel.setFont(Font(10.0f));
  //  updateVolumeLabel();
  //  addAndMakeVisible(volumeLabel);
}

RowHeader::~RowHeader() {
  pluginEditorWindow = nullptr; // Close any open editor window
  loadPluginButton.removeListener(this);
  editPluginButton.removeListener(this);
  removePluginButton.removeListener(this);
  volumeSlider.removeListener(this);
}

//==============================================================================
void RowHeader::paint(Graphics &g) {
  auto bgColour = selected ? Colours::steelblue.darker(0.3f)
                           : Colours::darkgrey.brighter(0.1f);

  if (pluginEditorWindow) {
    bgColour = bgColour.brighter(0.3f);
  }

  g.setColour(bgColour);
  g.fillRoundedRectangle(getLocalBounds().reduced(2).toFloat(), 4.0f);

  g.setColour(selected ? Colours::steelblue : Colours::grey);
  g.drawRoundedRectangle(getLocalBounds().reduced(2).toFloat(), 4.0f, 1.0f);
}

void RowHeader::resized() {
  auto bounds = getLocalBounds().reduced(4);
  macroToggle.setBounds(bounds.removeFromLeft(30));

  // Top row: name label, buttons
  auto topRow = bounds.removeFromTop(22);
  nameLabel.setBounds(topRow.removeFromLeft(50));
  removePluginButton.setBounds(topRow.removeFromRight(24));
  editPluginButton.setBounds(topRow.removeFromRight(30));
  loadPluginButton.setBounds(topRow.removeFromRight(35));

  bounds.removeFromTop(2);

  // Middle row: plugin name
  auto pluginRow = bounds.removeFromTop(16);
  pluginNameLabel.setBounds(pluginRow);

  bounds.removeFromTop(2);

  // Bottom row: volume slider and label
  auto volumeRow = bounds.removeFromTop(20);
  //  volumeLabel.setBounds(volumeRow.removeFromRight(50));
  volumeSlider.setBounds(volumeRow);
}

//==============================================================================
void RowHeader::setSelected(bool shouldBeSelected) {
  if (selected != shouldBeSelected) {
    selected = shouldBeSelected;
    repaint();
  }
}

MemoryBlock RowHeader::getPluginState() const {
  MemoryBlock state;
  if (plugin != nullptr) {
    plugin->getStateInformation(state);
  }
  return state;
}

void RowHeader::setVolumeDb(float db) {
  volumeDb = jlimit(-96.0f, 12.0f, db);
  volumeSlider.setValue(volumeDb, dontSendNotification);
  if (onVolumeChanged)
    onVolumeChanged(volumeDb);
}

// void RowHeader::updateVolumeLabel() {
//   if (volumeDb <= -96.0f) {
//     volumeLabel.setText("-inf dB", dontSendNotification);
//   } else {
//     volumeLabel.setText(String(volumeDb, 1) + " dB", dontSendNotification);
//   }
// }

//==============================================================================
void RowHeader::buttonClicked(Button *button) {
  if (button == &loadPluginButton) {
    showPluginMenu();
  } else if (button == &editPluginButton) {
    showPluginEditor();
  } else if (button == &removePluginButton) {
    removePlugin();
  }
}

void RowHeader::sliderValueChanged(Slider *slider) {
  if (slider == &volumeSlider) {
    volumeDb = static_cast<float>(slider->getValue());
      setVolumeDb(volumeDb);
  }
}

void RowHeader::showPluginMenu() {
  PopupMenu menu;
  pluginsManager.addPluginsToMenu(menu);

  menu.showMenuAsync(
      PopupMenu::Options().withTargetComponent(&loadPluginButton),
      [this](int result) {
        if (result > 0) {
          auto desc = pluginsManager.getChosenType(result);
          loadPlugin(desc);
        }
      });
}

void RowHeader::loadPlugin(const ayra::PluginDescriptionAndPreference &desc) {
  String errorMessage;

  plugin = pluginsManager.createPluginInstance(desc,
                                               48000.0, // Default sample rate
                                               512,     // Default block size
                                               errorMessage);

  if (plugin != nullptr) {
    pluginDesc = desc.pluginDescription;
    pluginNameLabel.setText(plugin->getName(), dontSendNotification);
    pluginNameLabel.setColour(Label::textColourId, Colours::white);
    editPluginButton.setEnabled(true);
    removePluginButton.setEnabled(true);

    if (onPluginLoaded)
      onPluginLoaded();
  } else {
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
                                     "Plugin Load Error", errorMessage);
  }
}

void RowHeader::removePlugin() {
  pluginEditorWindow = nullptr; // Close any open editor
  plugin.reset();
  pluginDesc = {};
  pluginNameLabel.setText("No plugin", dontSendNotification);
  pluginNameLabel.setColour(Label::textColourId, Colours::grey);
  editPluginButton.setEnabled(false);
  removePluginButton.setEnabled(false);
}

void RowHeader::setPlugin(std::unique_ptr<AudioPluginInstance> newPlugin,
                          const PluginDescription &desc) {
  pluginEditorWindow = nullptr; // Close any existing editor
  plugin = std::move(newPlugin);
  pluginDesc = desc;

  if (plugin != nullptr) {
    pluginNameLabel.setText(plugin->getName(), dontSendNotification);
    pluginNameLabel.setColour(Label::textColourId, Colours::white);
    editPluginButton.setEnabled(true);
    removePluginButton.setEnabled(true);

    if (onPluginLoaded)
      onPluginLoaded();
  }
}

void RowHeader::showPluginEditor() {
  if (plugin == nullptr)
    return;

  // If window already exists, bring to front
  if (pluginEditorWindow != nullptr) {
    pluginEditorWindow->setVisible(true);
    pluginEditorWindow->toFront(true);
    repaint();
    return;
  }

  // Create plugin editor
  auto *editor = plugin->createEditor();
  if (editor == nullptr) {
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon, "No Editor",
                                     "This plugin does not have a GUI editor");
    return;
  }

  // Create a custom window class that properly handles close
  class PluginEditorWindow : public DocumentWindow {
  public:
    PluginEditorWindow(const String &name,
                       std::unique_ptr<DocumentWindow> *ownerPtr)
        : DocumentWindow(name, Colours::darkgrey, DocumentWindow::closeButton),
          owner(ownerPtr) {
      setUsingNativeTitleBar(true);
    }

    void closeButtonPressed() override {
      // Just hide, don't delete - owner will manage lifetime
      setVisible(false);
    }

  private:
    std::unique_ptr<DocumentWindow> *owner;
  };

  // Create floating window for the editor
  auto *window = new PluginEditorWindow(
      plugin->getName() + " - " + nameLabel.getText(), &pluginEditorWindow);
  pluginEditorWindow.reset(window);
  window->setContentOwned(editor, true);
  window->setResizable(false, false);
  window->centreWithSize(editor->getWidth(), editor->getHeight());
  window->setVisible(true);
  window->toFront(true);
  repaint();
}

//==============================================================================
void RowHeader::mouseDown(const MouseEvent &e) {
  if (onSelected)
    onSelected();

  Component::mouseDown(e);
}

void RowHeader::mouseDoubleClick(const MouseEvent &e) { showPluginEditor(); }
