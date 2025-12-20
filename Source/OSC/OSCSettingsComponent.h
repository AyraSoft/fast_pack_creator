/*
  ==============================================================================

    OSCSettingsComponent.h
    Fast Pack Creator - MIDI Batch Renderer

    UI for OSC Settings

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include "OSCController.h"
#include <JuceHeader.h>

//==============================================================================
class OSCSettingsComponent : public Component {
public:
  //==============================================================================
  OSCSettingsComponent(OSCController &controller);
  ~OSCSettingsComponent() override = default;

  //==============================================================================
  void paint(Graphics &) override;
  void resized() override;

private:
  //==============================================================================
  OSCController &oscController;

  ToggleButton enableToggle{"Enable OSC"};
  Label portLabel{{}, "Port:"};
  TextEditor portEditor;
  TextButton applyButton{"Apply"};
  Label statusLabel{{}, "Status: Disconnected"};

  // Protocol info
  Label protocolInfoLabel;

  void updateStatus();

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OSCSettingsComponent)
};
