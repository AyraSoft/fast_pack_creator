/*
  ==============================================================================

    ColumnHeader.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class ColumnHeader : public Component {
public:
  //==============================================================================
  ColumnHeader(int colIndex, const File &midiFile);
  ~ColumnHeader() override = default;

  //==============================================================================
  void paint(Graphics &) override;
  void resized() override;

  //==============================================================================
  int getPitchOffset() const;
  float getVelocityMultiplier() const;

  const File &getMidiFile() const { return midiFile; }
  int getColumnIndex() const { return columnIndex; }

  // Callback for macro toggle (toggle all cells in this column)
  std::function<void()> onMacroToggle;

private:
  //==============================================================================
  int columnIndex;
  File midiFile;

  TextButton macroToggle{""}; // 30x30 toggle button at top
  Label fileNameLabel;

  //  Label pitchLabel{{}, "Pitch:"};
  Slider pitchSlider;

  //  Label velocityLabel{{}, "Velocity:"};
  Slider velocitySlider;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ColumnHeader)
};
