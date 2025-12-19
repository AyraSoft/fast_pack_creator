/*
  ==============================================================================

    ConfigurationPanel.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "ConfigurationPanel.h"

//==============================================================================
ConfigurationPanel::ConfigurationPanel() {
  // Variations slider
  variationsSlider.setRange(1, 100, 1);
  variationsSlider.setValue(10, dontSendNotification);
  variationsSlider.setSliderStyle(Slider::LinearHorizontal);
  variationsSlider.setTextBoxStyle(Slider::TextBoxRight, false, 50, 24);
  variationsSlider.onValueChange = [this] {
    if (onVariationsChanged)
      onVariationsChanged(static_cast<int>(variationsSlider.getValue()));
  };

  addAndMakeVisible(variationsLabel);
  addAndMakeVisible(variationsSlider);

  // BPM slider - immediate updates on change
  bpmSlider.setRange(20.0, 300.0, 0.1);
  bpmSlider.setValue(120.0, dontSendNotification);
  bpmSlider.setSliderStyle(Slider::LinearBar);

  bpmSlider.onValueChange = [this] {
    if (onBpmChanged)
      onBpmChanged(bpmSlider.getValue());
  };

  addAndMakeVisible(bpmLabel);
  addAndMakeVisible(bpmSlider);

  // Variation 1
  addAndMakeVisible(variationBpm1);
  variationBpm1.setToggleState(false, dontSendNotification); // Default on

  addAndMakeVisible(variationBpm1Slider);
  variationBpm1Slider.setSliderStyle(Slider::LinearBar);
  variationBpm1Slider.setRange(20.0, 300.0, 1.0);
  variationBpm1Slider.setValue(80.0, dontSendNotification);

  // Variation 2
  addAndMakeVisible(variationBpm2);
  variationBpm2.setToggleState(false, dontSendNotification);

  addAndMakeVisible(variationBpm2Slider);
  variationBpm2Slider.setSliderStyle(Slider::LinearBar);
  variationBpm2Slider.setRange(20.0, 300.0, 1.0);
  variationBpm2Slider.setValue(160.0, dontSendNotification);

  addAndMakeVisible(loop);
  loop.setToggleState(true, dontSendNotification);

  addAndMakeVisible(seamlessLoop);
  seamlessLoop.setToggleState(
      true, dontSendNotification); // Enable by default for perfect loops

  addAndMakeVisible(applyNormalization);
  applyNormalization.setToggleState(true, dontSendNotification);
  addAndMakeVisible(normalizationHeadroom);
  normalizationHeadroom.setSliderStyle(Slider::LinearBar);
  normalizationHeadroom.setRange(-20.0, 0.0, 1.0);
  normalizationHeadroom.setValue(-12.0, dontSendNotification);

  // Folder selection
  selectFolderButton.onClick = [this] {
    fileChooser = std::make_unique<FileChooser>(
        "Select MIDI Folder", File{"~/Users/Lavori"}, "", true);

    fileChooser->launchAsync(FileBrowserComponent::openMode |
                                 FileBrowserComponent::canSelectDirectories,
                             [this](const FileChooser &fc) {
                               if (fc.getResults().size() > 0) {
                                 File folder = fc.getResult();
                                 folderPathLabel.setText(
                                     folder.getFullPathName(),
                                     dontSendNotification);

                                 if (onMidiFolderSelected)
                                   onMidiFolderSelected(folder);
                               }
                             });
  };

  folderPathLabel.setColour(Label::textColourId, Colours::grey);
  folderPathLabel.setText("No folder selected", dontSendNotification);

  addAndMakeVisible(selectFolderButton);
  addAndMakeVisible(folderPathLabel);
}

//==============================================================================
void ConfigurationPanel::paint(Graphics &g) {
  g.setColour(getLookAndFeel()
                  .findColour(ResizableWindow::backgroundColourId)
                  .brighter(0.1f));
  g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.0f);
}

void ConfigurationPanel::resized() {
  auto bounds = getLocalBounds().reduced(10);

  auto row1 = bounds.removeFromTop(28);
  variationsLabel.setBounds(row1.removeFromLeft(80));
  variationsSlider.setBounds(row1.removeFromLeft(200));

  row1.removeFromLeft(20);
  bpmLabel.setBounds(row1.removeFromLeft(40));
  bpmSlider.setBounds(row1.removeFromLeft(100));

  // Variations controls
  row1.removeFromLeft(20);
  variationBpm1.setBounds(row1.removeFromLeft(80));
  variationBpm1Slider.setBounds(row1.removeFromLeft(60));

  row1.removeFromLeft(10);
  variationBpm2.setBounds(row1.removeFromLeft(80));
  variationBpm2Slider.setBounds(row1.removeFromLeft(60));

  row1.removeFromLeft(40);
  loop.setBounds(row1.removeFromLeft(60));
  seamlessLoop.setBounds(row1.removeFromLeft(80));

  row1.removeFromLeft(40);
  applyNormalization.setBounds(row1.removeFromLeft(80));
  normalizationHeadroom.setBounds(row1.removeFromLeft(80));

  bounds.removeFromTop(5);

  auto row2 = bounds.removeFromTop(28);
  selectFolderButton.setBounds(row2.removeFromLeft(150));
  row2.removeFromLeft(10);
  folderPathLabel.setBounds(row2);
}

//==============================================================================
void ConfigurationPanel::setNumVariations(int num) {
  variationsSlider.setValue(num, dontSendNotification);
}

void ConfigurationPanel::setBpm(double newBpm) {
  bpmSlider.setValue(newBpm, dontSendNotification);
}

bool ConfigurationPanel::isVariation1Enabled() const {
  return variationBpm1.getToggleState();
}
double ConfigurationPanel::getVariation1Bpm() const {
  return variationBpm1Slider.getValue();
}
bool ConfigurationPanel::isVariation2Enabled() const {
  return variationBpm2.getToggleState();
}
double ConfigurationPanel::getVariation2Bpm() const {
  return variationBpm2Slider.getValue();
}

void ConfigurationPanel::reset() {
  variationsSlider.setValue(10, dontSendNotification);
  bpmSlider.setValue(120.0, dontSendNotification);
  folderPathLabel.setText("No folder selected", dontSendNotification);
}
