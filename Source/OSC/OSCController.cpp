/*
  ==============================================================================

    OSCController.cpp
    Fast Pack Creator - MIDI Batch Renderer

    OSC Remote Control for TouchOSC integration

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "OSCController.h"

//==============================================================================
OSCController::OSCController() { addListener(this); }

OSCController::~OSCController() {
  disconnect();
  removeListener(this);
}

//==============================================================================
bool OSCController::connect(int port) {
  disconnect();

  if (juce::OSCReceiver::connect(port)) {
    currentPort = port;
    connected = true;
    DBG("OSC: Connected on port " + juce::String(port));
    return true;
  }

  DBG("OSC: Failed to connect on port " + juce::String(port));
  return false;
}

void OSCController::disconnect() {
  if (connected) {
    juce::OSCReceiver::disconnect();
    connected = false;
    DBG("OSC: Disconnected");
  }
}

//==============================================================================
void OSCController::oscMessageReceived(const juce::OSCMessage &message) {
  juce::String address = message.getAddressPattern().toString();
  DBG("OSC: Received " + address);

  // Parse address pattern
  juce::StringArray parts;
  parts.addTokens(address, "/", "");
  parts.removeEmptyStrings();

  if (parts.size() == 0)
    return;

  // /cell/play/{row}/{column}
  // /cell/stop/{row}/{column}
  // /cell/trigger/{row}/{column} - value 1=play, 0=stop
  if (parts[0] == "cell" && parts.size() >= 4) {
    int row = parts[2].getIntValue();
    int column = parts[3].getIntValue();

    if (parts[1] == "play") {
      if (onCellPlay)
        juce::MessageManager::callAsync([this, row, column] {
          if (onCellPlay)
            onCellPlay(row, column);
        });
    } else if (parts[1] == "stop") {
      if (onCellStop)
        juce::MessageManager::callAsync([this, row, column] {
          if (onCellStop)
            onCellStop(row, column);
        });
    } else if (parts[1] == "trigger") {
      // TouchOSC push button: value 1 = pressed, value 0 = released
      float value = 0.0f;
      if (message.size() > 0 && message[0].isFloat32())
        value = message[0].getFloat32();
      else if (message.size() > 0 && message[0].isInt32())
        value = static_cast<float>(message[0].getInt32());

      if (value > 0.5f) {
        // Button pressed = play
        if (onCellPlay)
          juce::MessageManager::callAsync([this, row, column] {
            if (onCellPlay)
              onCellPlay(row, column);
          });
      } else {
        // Button released = stop
        if (onCellStop)
          juce::MessageManager::callAsync([this, row, column] {
            if (onCellStop)
              onCellStop(row, column);
          });
      }
    } else if (parts[1] == "toggle") {
      // Toggle = play (legacy)
      if (onCellPlay)
        juce::MessageManager::callAsync([this, row, column] {
          if (onCellPlay)
            onCellPlay(row, column);
        });
    }
  }
  // /plugin/gui/toggle/{row}
  // /plugin/gui/open/{row}
  // /plugin/gui/close/{row}
  else if (parts[0] == "plugin" && parts.size() >= 4) {
    if (parts[1] == "gui") {
      int row = parts[3].getIntValue();

      if (parts[2] == "toggle") {
        if (onPluginGuiToggle)
          juce::MessageManager::callAsync([this, row] {
            if (onPluginGuiToggle)
              onPluginGuiToggle(row);
          });
      } else if (parts[2] == "open") {
        if (onPluginGuiOpen)
          juce::MessageManager::callAsync([this, row] {
            if (onPluginGuiOpen)
              onPluginGuiOpen(row);
          });
      } else if (parts[2] == "close") {
        if (onPluginGuiClose)
          juce::MessageManager::callAsync([this, row] {
            if (onPluginGuiClose)
              onPluginGuiClose(row);
          });
      }
    }
  }
  // /panic
  else if (parts[0] == "panic") {
    if (onPanic)
      juce::MessageManager::callAsync([this] {
        if (onPanic)
          onPanic();
      });
  }
}

void OSCController::oscBundleReceived(const juce::OSCBundle &bundle) {
  for (auto &element : bundle) {
    if (element.isMessage())
      oscMessageReceived(element.getMessage());
    else if (element.isBundle())
      oscBundleReceived(element.getBundle());
  }
}
