/*
  ==============================================================================

    MidiGridModel.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class MidiGridModel : public ayra::GridComponent::Model {
public:
  //==============================================================================
  MidiGridModel() = default;
  ~MidiGridModel() override = default;

  //==============================================================================
  int numRows = 10;
  int numColumns = 0;

  static constexpr int ROW_HEADER_WIDTH = 200;
  static constexpr int COLUMN_HEADER_HEIGHT = 80;
  static constexpr int CELL_WIDTH = 120;
  static constexpr int CELL_HEIGHT = 80; // Increased for volume slider

  //==============================================================================
  int getNumRows() const override {
    // +1 for header row
    return numRows + 1;
  }

  int getNumColumns() const override {
    // +1 for header column
    return numColumns + 1;
  }

  int getRowSize(int row) const override {
    if (owner == nullptr)
      return (row == 0) ? COLUMN_HEADER_HEIGHT : CELL_HEIGHT;

    if (owner->getDirection() == ayra::GridComponent::vertical) {
      // Height of each row
      return (row == 0) ? COLUMN_HEADER_HEIGHT : CELL_HEIGHT;
    } else {
      // Width when horizontal
      return (row == 0) ? ROW_HEADER_WIDTH : CELL_WIDTH;
    }
  }

  int getColumnSize(int column) const override {
    if (owner == nullptr)
      return (column == 0) ? ROW_HEADER_WIDTH : CELL_WIDTH;

    if (owner->getDirection() == ayra::GridComponent::vertical) {
      // Width of each column
      return (column == 0) ? ROW_HEADER_WIDTH : CELL_WIDTH;
    } else {
      // Height when horizontal
      return (column == 0) ? COLUMN_HEADER_HEIGHT : CELL_HEIGHT;
    }
  }

private:
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiGridModel)
};
