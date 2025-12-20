/*
  ==============================================================================

    OSCSettingsComponent.cpp
    Fast Pack Creator - MIDI Batch Renderer

    UI for OSC Settings

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "OSCSettingsComponent.h"

//==============================================================================
OSCSettingsComponent::OSCSettingsComponent(OSCController &controller)
    : oscController(controller) {

  // Enable toggle
  enableToggle.setToggleState(oscController.isConnected(),
                              dontSendNotification);
  enableToggle.onClick = [this] {
    if (enableToggle.getToggleState()) {
      int port = portEditor.getText().getIntValue();
      if (port > 0 && port < 65536) {
        oscController.connect(port);
      }
    } else {
      oscController.disconnect();
    }
    updateStatus();
  };
  addAndMakeVisible(enableToggle);

  // Port editor
  addAndMakeVisible(portLabel);
  portEditor.setText(String(oscController.getPort()), dontSendNotification);
  portEditor.setInputRestrictions(5, "0123456789");
  addAndMakeVisible(portEditor);

  // Apply button
  applyButton.onClick = [this] {
    int port = portEditor.getText().getIntValue();
    if (port > 0 && port < 65536) {
      if (oscController.connect(port)) {
        enableToggle.setToggleState(true, dontSendNotification);
      }
    }
    updateStatus();
  };
  addAndMakeVisible(applyButton);

  // Status label
  addAndMakeVisible(statusLabel);
  updateStatus();

  // Protocol info
  String protocolText = "OSC Protocol:\n"
                        "  /cell/play/{row}/{col}  - Play cell\n"
                        "  /cell/stop/{row}/{col}  - Stop cell\n"
                        "  /plugin/gui/toggle/{row} - Toggle plugin GUI\n"
                        "  /plugin/gui/open/{row}  - Open plugin GUI\n"
                        "  /plugin/gui/close/{row} - Close plugin GUI\n"
                        "  /panic                  - Stop all";
  protocolInfoLabel.setText(protocolText, dontSendNotification);
  protocolInfoLabel.setJustificationType(Justification::topLeft);
  protocolInfoLabel.setFont(
      Font(Font::getDefaultMonospacedFontName(), 12.0f, Font::plain));
  addAndMakeVisible(protocolInfoLabel);
}

//==============================================================================
void OSCSettingsComponent::paint(Graphics &g) {
  g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void OSCSettingsComponent::resized() {
  auto bounds = getLocalBounds().reduced(20);

  auto row1 = bounds.removeFromTop(30);
  enableToggle.setBounds(row1.removeFromLeft(120));
  row1.removeFromLeft(20);
  portLabel.setBounds(row1.removeFromLeft(40));
  portEditor.setBounds(row1.removeFromLeft(80));
  row1.removeFromLeft(10);
  applyButton.setBounds(row1.removeFromLeft(80));

  bounds.removeFromTop(10);
  statusLabel.setBounds(bounds.removeFromTop(25));

  bounds.removeFromTop(20);
  protocolInfoLabel.setBounds(bounds);
}

//==============================================================================
void OSCSettingsComponent::updateStatus() {
  if (oscController.isConnected()) {
    statusLabel.setText("Status: Connected on port " +
                            String(oscController.getPort()),
                        dontSendNotification);
    statusLabel.setColour(Label::textColourId, Colours::green);
  } else {
    statusLabel.setText("Status: Disconnected", dontSendNotification);
    statusLabel.setColour(Label::textColourId, Colours::red);
  }
}
