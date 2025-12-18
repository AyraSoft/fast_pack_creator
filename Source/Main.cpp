/*
  ==============================================================================

    Main.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "MainComponent.h"
#include <JuceHeader.h>

//==============================================================================
class FastPackCreatorApplication : public JUCEApplication {
public:
  //==============================================================================
  FastPackCreatorApplication() {}

  const String getApplicationName() override {
    return ProjectInfo::projectName;
  }
  const String getApplicationVersion() override {
    return ProjectInfo::versionString;
  }
  bool moreThanOneInstanceAllowed() override { return true; }

  //==============================================================================
  void initialise(const String &commandLine) override {
    // Check if we're being launched as a plugin scanning subprocess
    // The subprocess scanner launches this app with special command line
    // arguments
    if (commandLine.contains(PROCESS_UID)) {
      // We're running as a subprocess worker for plugin scanning
      pluginScannerSubprocess =
          std::make_unique<ayra::PluginScannerSubprocess>();
      if (pluginScannerSubprocess->initialiseFromCommandLine(commandLine,
                                                             PROCESS_UID)) {
        // Successfully initialized as subprocess - don't create main window
        return;
      }
      // Failed to initialize as subprocess - continue with normal app
      // initialization
      pluginScannerSubprocess.reset();
    }

    // Initialize app properties for plugin management
    ayra::app_properties->initialize("Fast Pack Creator");

    mainWindow.reset(new MainWindow(getApplicationName()));
  }

  void shutdown() override { mainWindow = nullptr; }

  //==============================================================================
  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const String &commandLine) override {
    ignoreUnused(commandLine);
  }

  //==============================================================================
  class MainWindow : public DocumentWindow {
  public:
    MainWindow(String name)
        : DocumentWindow(
              name,
              Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainComponent(), true);

#if JUCE_IOS || JUCE_ANDROID
      setFullScreen(true);
#else
      setResizable(true, true);
      centreWithSize(getWidth(), getHeight());
#endif

      setVisible(true);
    }

    void closeButtonPressed() override {
      JUCEApplication::getInstance()->systemRequestedQuit();
    }

  private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

private:
  std::unique_ptr<MainWindow> mainWindow;
  std::unique_ptr<ayra::PluginScannerSubprocess> pluginScannerSubprocess;
};

//==============================================================================
START_JUCE_APPLICATION(FastPackCreatorApplication)
