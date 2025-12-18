/*
  ==============================================================================

    CellPad.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "CellPad.h"

//==============================================================================
CellPad::CellPad(int row, int column) : rowIndex(row), columnIndex(column) {
  playButton.addListener(this);
  playButton.setColour(TextButton::buttonColourId, Colours::darkgreen);
  addAndMakeVisible(playButton);

  stopButton.addListener(this);
  stopButton.setColour(TextButton::buttonColourId, Colours::darkred);
  addAndMakeVisible(stopButton);
    
    addAndMakeVisible(renderizable);
    renderizable.setToggleState(true, dontSendNotification);
}

CellPad::~CellPad() {
  playButton.removeListener(this);
  stopButton.removeListener(this);
}

//==============================================================================
void CellPad::paint(Graphics &g) {
  auto bgColour = isPlaying ? Colours::darkgreen.withAlpha(0.3f)
                            : Colours::transparentBlack;

  g.setColour(bgColour);
  g.fillRoundedRectangle(getLocalBounds().reduced(2).toFloat(), 4.0f);

  g.setColour(Colours::grey.withAlpha(0.5f));
  g.drawRoundedRectangle(getLocalBounds().reduced(2).toFloat(), 4.0f, 1.0f);
}

void CellPad::resized() {
  auto bounds = getLocalBounds().reduced(4);

  int buttonWidth = (bounds.getWidth() - 4) / 2;

    renderizable.setBounds(bounds.removeFromTop(30));
  playButton.setBounds(bounds.removeFromLeft(buttonWidth));
  bounds.removeFromLeft(4);
  stopButton.setBounds(bounds.removeFromLeft(buttonWidth));
}

//==============================================================================
void CellPad::setPlaying(bool playing) {
  if (isPlaying != playing) {
    isPlaying = playing;
    repaint();
  }
}

//==============================================================================
void CellPad::buttonClicked(Button *button) {
  if (button == &playButton) {
    if (onPlay)
      onPlay(rowIndex, columnIndex);
  } else if (button == &stopButton) {
    if (onStop)
      onStop(rowIndex, columnIndex);
  }
}

void CellPad::setRowAndColumn(int row, int column) {
  rowIndex = row;
  columnIndex = column;
}
