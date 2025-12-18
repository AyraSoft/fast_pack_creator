/*
  ==============================================================================

    CellPad.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class CellPad : public Component, public Button::Listener {
public:
  //==============================================================================
  CellPad(int row, int column);
  ~CellPad() override;

  //==============================================================================
  void paint(Graphics &) override;
  void resized() override;

  //==============================================================================
  std::function<void(int row, int column)> onPlay;
  std::function<void(int row, int column)> onStop;

  void setPlaying(bool playing);
  void setRowAndColumn(int row, int column);

    int getRow() const    { return rowIndex; }
    int getColumn() const { return columnIndex; }
    
  //==============================================================================
  // Button::Listener
  void buttonClicked(Button *) override;
    
    //==============================================================================
    
    void setRenderizable(bool on) { renderizable.setToggleState(on, dontSendNotification); }
    bool isRenderizable() const { return renderizable.getToggleState(); }
    
    ToggleButton renderizable {"Renderizable"};
    
private:
  //==============================================================================
  int rowIndex;
  int columnIndex;
  bool isPlaying = false;

  TextButton playButton{juce::CharPointer_UTF8("\xe2\x96\xb6")}; // ▶
  TextButton stopButton{juce::CharPointer_UTF8("\xe2\x96\xa0")}; // ■


  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CellPad)
};
