/*
  ==============================================================================

    MainComponent.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include "Audio/PluginHost.h"
#include "ConfigurationPanel.h"
#include "MidiGrid/MidiGridComponent.h"
#include "OSC/OSCController.h"
#include "ProjectSerializer.h"
#include "Rendering/ParallelBatchRenderer.h"
#include <JuceHeader.h>

//==============================================================================
class MainComponent : public Component,
                      public ChangeListener,
                      public ayra::PluginsManager::Listener,
                      public MenuBarModel {
public:
  //==============================================================================
  MainComponent();
  ~MainComponent() override;

  //==============================================================================
  void paint(Graphics &) override;
  void resized() override;

  //==============================================================================
  // MenuBarModel
  StringArray getMenuBarNames() override;
  PopupMenu getMenuForIndex(int menuIndex, const String &menuName) override;
  void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

private:
  //==============================================================================
  // Audio System
  AudioDeviceManager deviceManager;
  ayra::PluginsManager pluginsManager;
  std::unique_ptr<PluginHost> pluginHost;
  AudioSourcePlayer audioSourcePlayer; // Connects PluginHost to audio output

  //==============================================================================
  // UI Components
  ConfigurationPanel configPanel;
  std::unique_ptr<MidiGridComponent> gridComponent;

  TextButton renderButton{"Render All"};
  TextButton batchNormalization{"Batch Normalization"};

  std::unique_ptr<Component> pluginListWindow;
  std::unique_ptr<MenuBarComponent> menuBar;

  // OSC Remote Control
  OSCController oscController;

  // Rendering
  struct RenderPass {
    double bpm;
    String suffix; // e.g., "" or " [Var1]"
  };
  std::deque<RenderPass> renderQueue;
  double initialBpm = 120.0;
  void processNextRenderPass(const File &outputDir);

  std::unique_ptr<ParallelBatchRenderer> parallelRenderer;
  std::unique_ptr<DialogWindow> progressWindow;
  std::unique_ptr<ProgressBar> progressBar;
  std::unique_ptr<FileChooser> fileChooser;
  double renderProgress = 0.0;
  File currentOutputDir; // Stored for multi-pass rendering

  // Level meter
  foleys::LevelMeterLookAndFeel meterLnF;
  foleys::LevelMeter levelMeter{foleys::LevelMeter::Default};
  Slider masterVolume{"MasterVolume"};

  //==============================================================================
  // State
  Array<File> midiFiles;
  int numVariations = 10;
  double bpm = 120.0;
  File midiFolder;

  // Project state
  File currentProjectFile;
  bool projectModified = false;

  //==============================================================================
  // Methods
  void loadMidiFolder(const File &folder);
  void filterShortMidiFiles(const File &folder); // Remove MIDI files < 4 bars
  void rebuildGrid();
  void startRender();
  void showAudioSettings();
  void showPluginList();
  void showOscSettings();
  void runBatchNormalization(
      const File &outputDir); // Post-render LUFS normalization

  // Project save/load
  void newProject();
  void saveProject();
  void saveProjectAs();
  void loadProject();
  ProjectSerializer::ProjectData gatherProjectData();
  void applyProjectData(const ProjectSerializer::ProjectData &data);

  // ChangeListener
  void changeListenerCallback(ChangeBroadcaster *source) override;

  // PluginsManager::Listener
  void onPluginListChanged(ayra::PluginsManager *) override;
  void onScanFinish(ayra::PluginsManager *) override;

  //==============================================================================
  enum MenuIDs { FileNew = 1, FileSave, FileSaveAs, FileLoad };

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
