/*
  ==============================================================================

    ColumnHeader.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "ColumnHeader.h"

//==============================================================================
ColumnHeader::ColumnHeader(int colIndex, const File &file)
    : columnIndex(colIndex), midiFile(file) {
  // Macro toggle button (30x30) for toggling all cells in this column
  macroToggle.onClick = [this] {
    if (onMacroToggle)
      onMacroToggle();
  };
  addAndMakeVisible(macroToggle);

  // File name label
  fileNameLabel.setText(midiFile.getFileNameWithoutExtension(),
                        dontSendNotification);
  fileNameLabel.setJustificationType(Justification::centred);
  fileNameLabel.setFont(Font(12.0f, Font::bold));
  addAndMakeVisible(fileNameLabel);

  // Pitch slider: only 3 values (-12, 0, +12)
  pitchSlider.setRange(-12, 12, 12);
  pitchSlider.textFromValueFunction = [](double value) {
    return "pitch " + String(static_cast<int>(value));
  };
  pitchSlider.setValue(0, dontSendNotification);
  pitchSlider.setSliderStyle(Slider::LinearBar);
  pitchSlider.setNumDecimalPlacesToDisplay(0);

  addAndMakeVisible(pitchSlider);

  // Velocity slider: 0.01 to 2.00, step 0.01
  velocitySlider.setRange(0.01, 2.0, 0.01);
  velocitySlider.textFromValueFunction = [](double value) {
    return "vel * " + String(value, 2);
  };
  velocitySlider.setValue(1.0, dontSendNotification);
  velocitySlider.setSliderStyle(Slider::LinearBar);
  velocitySlider.setNumDecimalPlacesToDisplay(2);

  addAndMakeVisible(velocitySlider);
}

//==============================================================================
void ColumnHeader::paint(Graphics &g) {
  g.setColour(Colours::darkgrey.brighter(0.1f));
  g.fillRoundedRectangle(getLocalBounds().reduced(2).toFloat(), 4.0f);

  g.setColour(Colours::grey);
  g.drawRoundedRectangle(getLocalBounds().reduced(2).toFloat(), 4.0f, 1.0f);
}

void ColumnHeader::resized() {
  auto bounds = getLocalBounds().reduced(4);

  // Macro toggle at top (30x30)
  macroToggle.setBounds(bounds.removeFromTop(20).withSizeKeepingCentre(20, 20));
  bounds.removeFromTop(2);

  fileNameLabel.setBounds(bounds.removeFromTop(14));

  bounds.removeFromTop(2);

  auto pitchRow = bounds.removeFromTop(16);
  //  pitchLabel.setBounds(pitchRow.removeFromLeft(35));
  pitchSlider.setBounds(pitchRow);

  bounds.removeFromTop(2);

  auto velRow = bounds.removeFromTop(16);
  //  velocityLabel.setBounds(velRow.removeFromLeft(50));
  velocitySlider.setBounds(velRow);
}

//==============================================================================
int ColumnHeader::getPitchOffset() const {
  return static_cast<int>(pitchSlider.getValue());
}

float ColumnHeader::getVelocityMultiplier() const {
  return static_cast<float>(velocitySlider.getValue());
}
