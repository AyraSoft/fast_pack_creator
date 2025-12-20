/*
  ==============================================================================

    OSCController.h
    Fast Pack Creator - MIDI Batch Renderer

    OSC Remote Control for TouchOSC integration

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class OSCController : public juce::OSCReceiver,
                      public juce::OSCReceiver::Listener<
                          juce::OSCReceiver::MessageLoopCallback> {
public:
  //==============================================================================
  OSCController();
  ~OSCController() override;

  //==============================================================================
  bool connect(int port);
  void disconnect();
  bool isConnected() const { return connected; }
  int getPort() const { return currentPort; }

  //==============================================================================
  // Callbacks for external actions
  std::function<void(int row, int column)> onCellPlay;
  std::function<void(int row, int column)> onCellStop;
  std::function<void(int row)> onPluginGuiToggle;
  std::function<void(int row)> onPluginGuiOpen;
  std::function<void(int row)> onPluginGuiClose;
  std::function<void()> onPanic;

private:
  //==============================================================================
  void oscMessageReceived(const juce::OSCMessage &message) override;
  void oscBundleReceived(const juce::OSCBundle &bundle) override;

  //==============================================================================
  bool connected = false;
  int currentPort = 9000; // Default OSC port

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OSCController)
};
