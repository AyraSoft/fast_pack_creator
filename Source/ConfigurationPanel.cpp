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
  variationBpm1.setToggleState(false, dontSendNotification);

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
  seamlessLoop.setToggleState(true, dontSendNotification);

  addAndMakeVisible(applyNormalization);
  applyNormalization.setToggleState(true, dontSendNotification);
  addAndMakeVisible(normalizationHeadroom);
  normalizationHeadroom.setSliderStyle(Slider::LinearBar);
  normalizationHeadroom.setRange(-20.0, 0.0, 1.0);
  normalizationHeadroom.setValue(-12.0, dontSendNotification);

  // Progress type combobox
  addAndMakeVisible(progressType);
  progressType.addItem("Independent",
                       static_cast<int>(PlayheadMode::Independent));
  progressType.addItem("At Trigger", static_cast<int>(PlayheadMode::AtTrigger));
  progressType.addItem("No Moving", static_cast<int>(PlayheadMode::NoMoving));
  progressType.setSelectedId(static_cast<int>(PlayheadMode::Independent),
                             dontSendNotification);
  progressType.onChange = [this] {
    auto mode = getPlayheadMode();

    // Handle mode change
    if (mode == PlayheadMode::Independent) {
      startIndependentPlayhead();
    } else {
      stopIndependentPlayhead();
      if (mode == PlayheadMode::NoMoving) {
        // Reset to 0 when switching to NoMoving
        independentPpqPosition = 0.0;
        progressPlayhead.setValue(0.0, dontSendNotification);
      }
    }

    if (onPlayheadModeChanged)
      onPlayheadModeChanged(mode);
  };

  // Progress playhead slider (read-only display)
  addAndMakeVisible(progressPlayhead);
  progressPlayhead.setSliderStyle(Slider::LinearBar);
  progressPlayhead.setRange(0.0, 16.0, 0.01);
  progressPlayhead.setValue(0.0, dontSendNotification);
  progressPlayhead.setInterceptsMouseClicks(false, false);
  progressPlayhead.setTextBoxStyle(Slider::TextBoxLeft, true, 80, 20);

  // Start independent playhead by default
  startIndependentPlayhead();

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
  progressType.setBounds(row2.removeFromLeft(120));
  row2.removeFromLeft(10);
  progressPlayhead.setBounds(row2.removeFromLeft(200));
  row2.removeFromLeft(10);
  folderPathLabel.setBounds(row2);
}

//==============================================================================
void ConfigurationPanel::timerCallback() {
  // Independent mode: cycle playhead from 0 to 16 bars
  double currentTime = Time::getMillisecondCounterHiRes() / 1000.0;

  if (lastTimerCallbackTime > 0.0) {
    double deltaTime = currentTime - lastTimerCallbackTime;
    double bpm = bpmSlider.getValue();
    double beatsPerSecond = bpm / 60.0;
    double ppqIncrement = deltaTime * beatsPerSecond;

    independentPpqPosition += ppqIncrement;

    // Cycle back to 0 after 16 bars (64 beats in 4/4)
    if (independentPpqPosition >= 64.0) {
      independentPpqPosition = 0.0;
    }

    // Update slider display (in bars, 0-16)
    progressPlayhead.setValue(independentPpqPosition / 4.0,
                              dontSendNotification);

    // Sync position to PluginHost
    if (onPlayheadPositionChanged)
      onPlayheadPositionChanged(independentPpqPosition);
  }

  lastTimerCallbackTime = currentTime;
}

void ConfigurationPanel::startIndependentPlayhead() {
  lastTimerCallbackTime = Time::getMillisecondCounterHiRes() / 1000.0;
  startTimerHz(60); // 60 FPS update
}

void ConfigurationPanel::stopIndependentPlayhead() {
  stopTimer();
  lastTimerCallbackTime = 0.0;
}

//==============================================================================
void ConfigurationPanel::setNumVariations(int num) {
  variationsSlider.setValue(num, dontSendNotification);
}

void ConfigurationPanel::setBpm(double newBpm) {
  bpmSlider.setValue(newBpm, dontSendNotification);
}

void ConfigurationPanel::setPlayheadPosition(double ppqPosition) {
  // Update the slider in bars (divide by 4 for 4/4 time signature)
  progressPlayhead.setValue(ppqPosition / 4.0, dontSendNotification);
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
  independentPpqPosition = 0.0;
  progressPlayhead.setValue(0.0, dontSendNotification);
}
