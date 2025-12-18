/*
  ==============================================================================

    MainComponent.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "MainComponent.h"

//==============================================================================
MainComponent::MainComponent() {
  // Initialize audio device
  auto savedState =
      ayra::app_properties->getUserSettings()->getXmlValue("audioDeviceState");
  deviceManager.initialise(0, 2, savedState.get(), true);

  // Setup plugins manager
  pluginsManager.loadAppProperties();
  pluginsManager.setCustomScanner();
  pluginsManager.addListener(this);

  // Restore previously scanned plugin list
  if (auto savedList =
          ayra::app_properties->getUserSettings()->getXmlValue("pluginList"))
    pluginsManager.getKnownPluginList().recreateFromXml(*savedList);

  // Create plugin host
  pluginHost = std::make_unique<PluginHost>(deviceManager, pluginsManager);

  // Connect PluginHost to audio output and enable MIDI input
  audioSourcePlayer.setSource(pluginHost.get());
  deviceManager.addAudioCallback(&audioSourcePlayer);
  pluginHost->setAcceptingMidiInput(true); // Enable MIDI input from keyboards

  // Setup configuration panel callbacks
  configPanel.onVariationsChanged = [this](int num) {
    numVariations = num;
    rebuildGrid();
  };

  configPanel.onBpmChanged = [this](double newBpm) {
    bpm = newBpm;
    // Update grid BPM immediately for playback
    if (gridComponent != nullptr)
      gridComponent->setBpm(bpm);
  };

  configPanel.onMidiFolderSelected = [this](const File &folder) {
    loadMidiFolder(folder);
  };

  addAndMakeVisible(configPanel);

  // Setup buttons
  renderButton.onClick = [this] { startRender(); };
  renderButton.setEnabled(false);
  addAndMakeVisible(renderButton);

  audioSettingsButton.onClick = [this] { showAudioSettings(); };
  addAndMakeVisible(audioSettingsButton);

  pluginListButton.onClick = [this] { showPluginList(); };
  addAndMakeVisible(pluginListButton);

  // Create menu bar
  menuBar = std::make_unique<MenuBarComponent>(this);
  addAndMakeVisible(menuBar.get());

  // Setup level meter
  levelMeter.setLookAndFeel(&meterLnF);
  levelMeter.setMeterSource(pluginHost->getMeterSource());
  addAndMakeVisible(levelMeter);

  addAndMakeVisible(masterVolume);
  masterVolume.setSliderStyle(Slider::Rotary);
  masterVolume.setRange(-96.0, 12.0, 0.1);
  masterVolume.setValue(0.0, dontSendNotification);
  masterVolume.setTextBoxStyle(Slider::TextEntryBoxPosition::TextBoxBelow,
                               false, 200, 30);
  masterVolume.setSkewFactorFromMidPoint(
      -12.0); // Make -12dB the mid point for better control
  masterVolume.onValueChange = [this] {
    pluginHost->setMasterGain(
        Decibels::decibelsToGain(static_cast<float>(masterVolume.getValue())));
  };

  configPanel.onMidiPanic = [this] { pluginHost->stopPlayback(); };

  setSize(1480, 1000);
}

MainComponent::~MainComponent() {
  // Disconnect audio before destroying PluginHost
  deviceManager.removeAudioCallback(&audioSourcePlayer);
  audioSourcePlayer.setSource(nullptr);

  // Clean up level meter look and feel
  levelMeter.setLookAndFeel(nullptr);

  pluginsManager.removeListener(this);

  // Save audio device state
  auto audioState = deviceManager.createStateXml();
  ayra::app_properties->getUserSettings()->setValue("audioDeviceState",
                                                    audioState.get());
  ayra::app_properties->getUserSettings()->saveIfNeeded();
}

//==============================================================================
void MainComponent::paint(Graphics &g) {
  g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void MainComponent::resized() {
  auto bounds = getLocalBounds();

  // Menu bar at top
  if (menuBar != nullptr)
    menuBar->setBounds(bounds.removeFromTop(
        LookAndFeel::getDefaultLookAndFeel().getDefaultMenuBarHeight()));

  bounds = bounds.reduced(10);

  // Top bar with config and buttons
  auto topBar = bounds.removeFromTop(80);

  auto buttonBar = topBar.removeFromRight(300);
  audioSettingsButton.setBounds(buttonBar.removeFromTop(25).reduced(2));
  pluginListButton.setBounds(buttonBar.removeFromTop(25).reduced(2));
  renderButton.setBounds(buttonBar.removeFromTop(25).reduced(2));

  configPanel.setBounds(topBar);

  bounds.removeFromTop(10);

  // Level meter on right side (30px wide)
  auto rightArea = bounds.removeFromRight(200);
  masterVolume.setBounds(rightArea.removeFromBottom(200));
  levelMeter.setBounds(rightArea);

  bounds.removeFromRight(5);

  // Grid component
  if (gridComponent != nullptr)
    gridComponent->setBounds(bounds);
}

//==============================================================================
void MainComponent::loadMidiFolder(const File &folder) {
  midiFolder = folder;
  midiFiles.clear();

  // Find all MIDI files
  for (const auto &entry : RangedDirectoryIterator(
           folder, false, "*.mid;*.midi", File::findFiles)) {
    midiFiles.add(entry.getFile());
  }

  // Sort by name
  midiFiles.sort();

  DBG("Found " << midiFiles.size() << " MIDI files");

  rebuildGrid();
}

void MainComponent::rebuildGrid() {
  if (midiFiles.isEmpty()) {
    gridComponent.reset();
    renderButton.setEnabled(false);
    resized();
    return;
  }

  // Create grid component
  gridComponent =
      std::make_unique<MidiGridComponent>(pluginsManager, *pluginHost);
  gridComponent->setMidiFiles(midiFiles);
  gridComponent->setNumVariations(numVariations);
  gridComponent->setBpm(bpm);
  gridComponent->rebuild(); // Build grid after all settings are applied

  addAndMakeVisible(*gridComponent);

  renderButton.setEnabled(true);

  resized();
}

void MainComponent::startRender() {
  if (gridComponent == nullptr || midiFiles.isEmpty())
    return;

  // Choose output directory
  FileChooser chooser("Select Output Directory",
                      File::getSpecialLocation(File::userDesktopDirectory));

  if (chooser.browseForDirectory()) {
    File outputDir = chooser.getResult();

    // Build render queue
    renderQueue.clear();
    initialBpm = bpm; // Save current state

    // Pass 1: Original BPM
    renderQueue.push_back({bpm, ""});

    // Pass 2: Variation 1
    if (configPanel.isVariation1Enabled()) {
      renderQueue.push_back({configPanel.getVariation1Bpm(), " [Var1]"});
    }

    // Pass 3: Variation 2
    if (configPanel.isVariation2Enabled()) {
      renderQueue.push_back({configPanel.getVariation2Bpm(), " [Var2]"});
    }

    // Start processing
    processNextRenderPass(outputDir);
  }
}

void MainComponent::processNextRenderPass(const File &outputDir) {
  if (renderQueue.empty()) {
    // All passes complete
    // Restore state
    bpm = initialBpm;
    pluginHost->setBpm(bpm);
    configPanel.setBpm(bpm);

    MessageManager::callAsync([this] {
      if (progressWindow) {
        progressWindow->setVisible(false);
        progressWindow.reset();
      }
      AlertWindow::showMessageBoxAsync(
          MessageBoxIconType::InfoIcon, "Rendering Complete",
          "All files have been rendered successfully!");
    });
    return;
  }

  auto pass = renderQueue.front();
  renderQueue.pop_front();

  // Set BPM for this pass
  bpm = pass.bpm;
  pluginHost->setBpm(bpm); // Ensure host is updated
  // We don't update configPanel UI here to avoid confusion or we could to show
  // progress? Let's keep UI static or maybe update it to show what's happening?
  // User requirement: "reimpostare tutti i bpm dei midi e passare il nuovo bpm
  // ai plugin"

  // Create parallel renderer
  ParallelBatchRenderer::RenderSettings settings;
  settings.sampleRate = 44100.0;
  settings.bitDepth = 24;
  settings.bpm = bpm;
  settings.silenceThresholdDb = -50.0f;

  parallelRenderer = std::make_unique<ParallelBatchRenderer>(
      pluginsManager, settings, outputDir);

  // Add all render jobs
  for (int col = 0; col < midiFiles.size(); ++col) {
    auto &midiFile = midiFiles.getReference(col);
    auto columnSettings = gridComponent->getColumnSettings(col);

    for (int row = 0; row < numVariations; ++row) {
      if (!gridComponent->isCellRenderizable(row, col))
        continue;

      auto rowData = gridComponent->getRowData(row);

      if (rowData.pluginDescription.name.isEmpty())
        continue; // Skip rows without plugins

      ParallelBatchRenderer::RenderJob job;
      job.rowIndex = row; // Important for ParallelBatchRenderer queueing
      job.columnIndex = col;
      job.midiFile = midiFile;
      job.variationName = rowData.name;
      job.pluginDesc = rowData.pluginDescription;
      job.pluginState = rowData.pluginState;
      job.pitchOffset = columnSettings.pitchOffset;
      job.velocityMultiplier = columnSettings.velocityMultiplier;
      job.volumeDb = rowData.volumeDb;

      // Output: "MidiName [RowName] [BPM].wav"
      // Example: "Holographic Sliders [Var1] [120 BPM].wav"
      // User requested: "nomeDelMidiFile [RowName] [BPM].wav" (plus Var suffix
      // from our queue logic?) Actually user example: "Holographic Sliders
      // [Var1] [120 BPM].wav" where [Var1] is likely the ROW name. Wait, user
      // said: "nomeDelMidiFile [RowName] [BPM].wav" If we are doing variations,
      // maybe the suffix belongs to the RowName? Ah, the user req said:
      // "nomeDelMidiFile [RowName] [BPM].wav"
      // And "reimpostare tutti i bpm dei midi e passare il nuovo bpm ai plugin
      // e rifare la renderizzazione. serve a creare la variazione di bpm." So
      // we are rendering the SAME row at DIFFERENT BPMs. So we should append
      // the BPM to the filename. The [Var1] in user example might be the Row
      // Name. The variation from BPM is implicit in the [BPM] tag? Or should we
      // add [Var1] text if valid? User Example: "Holographic Sliders [Var1]
      // [120 BPM].wav" My Logic: MidiName + " [" + RowName + "]" + pass.suffix?
      // + " [" + String(bpm) + " BPM].wav" User instructions imply the RowName
      // IS "[Var1]" (from RowHeader::nameLabel).

      File midiDir =
          outputDir.getChildFile(midiFile.getFileNameWithoutExtension());
      midiDir.createDirectory();

      String filename = midiFile.getFileNameWithoutExtension() + " [" +
                        rowData.name + "]" + " [" + String(bpm) + " BPM]" +
                        ".wav";

      job.outputFile = midiDir.getChildFile(filename);

      parallelRenderer->addJob(job);
    }
  }

  if (parallelRenderer->getTotalJobs() == 0) {
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon,
                                     "Nothing to Render",
                                     "No items selected for rendering.");
    return;
  }

  // Setup callbacks
  parallelRenderer->onProgress = [this](float progress) {
    MessageManager::callAsync([this, progress] {
      renderProgress = progress;
      // Repaint or update needed? ProgressBar pulls from renderProgress
      // double variable automatically if setup correctly
    });
  };

  parallelRenderer->onComplete = [this] {
    MessageManager::callAsync([this] {
      if (progressWindow) {
        progressWindow->setVisible(false);
        progressWindow.reset();
      }
      AlertWindow::showMessageBoxAsync(
          MessageBoxIconType::InfoIcon, "Rendering Complete",
          "All files have been rendered successfully!");

      parallelRenderer.reset(); // Clean up renderer
    });
  };

  parallelRenderer->onError = [this](const String &error) {
    MessageManager::callAsync([this, error] {
      if (progressWindow) {
        progressWindow->setVisible(false);
        progressWindow.reset();
      }
      AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
                                       "Rendering Error", error);
      // Don't reset renderer immediately so user can retry or check logs?
      // For now reset it.
      parallelRenderer.reset();
    });
  };

  // Show progress window
  renderProgress = 0.0;
  progressBar = std::make_unique<ProgressBar>(renderProgress);

  // Create a custom content component to hold the progress bar
  auto *content = new Component();
  content->setSize(400, 60);
  content->addAndMakeVisible(progressBar.get());
  progressBar->setBounds(20, 20, 360, 20);

  DialogWindow::LaunchOptions o;
  o.content.setOwned(content);
  o.dialogTitle = "Rendering Parallel";
  o.componentToCentreAround = this;
  o.dialogBackgroundColour =
      getLookAndFeel().findColour(ResizableWindow::backgroundColourId);
  o.escapeKeyTriggersCloseButton = false;
  o.useNativeTitleBar = true;
  o.resizable = false;

  progressWindow.reset(o.create());
  progressWindow->setVisible(true);

  parallelRenderer->startRendering();
}

void MainComponent::showAudioSettings() {
  auto *audioSettingsComp = new AudioDeviceSelectorComponent(
      deviceManager, 0, 2, 0, 2, true, true, true, false);

  audioSettingsComp->setSize(500, 450);

  DialogWindow::LaunchOptions o;
  o.content.setOwned(audioSettingsComp);
  o.dialogTitle = "Audio Settings";
  o.componentToCentreAround = this;
  o.dialogBackgroundColour =
      getLookAndFeel().findColour(ResizableWindow::backgroundColourId);
  o.escapeKeyTriggersCloseButton = true;
  o.useNativeTitleBar = false;
  o.resizable = false;

  o.launchAsync();
}

void MainComponent::showPluginList() {
  // Use the standard JUCE PluginListComponent directly
  auto *pluginListComp = new juce::PluginListComponent(
      pluginsManager.getFormatManager(), pluginsManager.getKnownPluginList(),
      juce::File(), // deadMansPedalFile
      ayra::app_properties->getUserSettings(), true);

  pluginListComp->setSize(600, 500);

  DialogWindow::LaunchOptions o;
  o.content.setOwned(pluginListComp);
  o.dialogTitle = "Plugin Scanner";
  o.componentToCentreAround = this;
  o.dialogBackgroundColour =
      getLookAndFeel().findColour(ResizableWindow::backgroundColourId);
  o.escapeKeyTriggersCloseButton = true;
  o.useNativeTitleBar = false;
  o.resizable = true;

  o.launchAsync();
}

//==============================================================================
void MainComponent::changeListenerCallback(ChangeBroadcaster *source) {
  ignoreUnused(source);
}

void MainComponent::onPluginListChanged(ayra::PluginsManager *) {
  // Save plugin list to app properties
  auto xml = pluginsManager.getKnownPluginList().createXml();
  ayra::app_properties->getUserSettings()->setValue("pluginList", xml.get());
  ayra::app_properties->getUserSettings()->saveIfNeeded();
}

void MainComponent::onScanFinish(ayra::PluginsManager *) {
  DBG("Plugin scan finished");
}

//==============================================================================
// MenuBarModel implementation
StringArray MainComponent::getMenuBarNames() { return {"File", "Utils"}; }

PopupMenu MainComponent::getMenuForIndex(int menuIndex, const String &) {
  PopupMenu menu;

  if (menuIndex == 0) // File menu
  {
    menu.addItem(FileNew, "New Project");
    menu.addSeparator();
    menu.addItem(FileSave, "Save", currentProjectFile.existsAsFile());
    menu.addItem(FileSaveAs, "Save As...");
    menu.addItem(FileLoad, "Load...");
  } else if (menuIndex == 1) {
    menu.addItem(1, "Panic");
  }

  return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int topLevelMenuIndex) {
  if (topLevelMenuIndex == 0) {
    switch (menuItemID) {
    case FileNew:
      newProject();
      break;
    case FileSave:
      saveProject();
      break;
    case FileSaveAs:
      saveProjectAs();
      break;
    case FileLoad:
      loadProject();
      break;
    default:
      break;
    }
  } else if (topLevelMenuIndex == 1) {
    if (configPanel.onMidiPanic)
      configPanel.onMidiPanic();
  }
}

//==============================================================================
// Project save/load
void MainComponent::newProject() {
  midiFiles.clear();
  midiFolder = File();
  numVariations = 10;
  bpm = 120.0;
  currentProjectFile = File();
  projectModified = false;

  gridComponent.reset();
  renderButton.setEnabled(false);

  configPanel.reset();
}

void MainComponent::saveProject() {
  if (!currentProjectFile.existsAsFile()) {
    saveProjectAs();
    return;
  }

  auto data = gatherProjectData();
  if (ProjectSerializer::saveProject(currentProjectFile, data)) {
    projectModified = false;
  } else {
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
                                     "Save Failed",
                                     "Could not save the project file.");
  }
}

void MainComponent::saveProjectAs() {
  auto chooser = std::make_shared<FileChooser>(
      "Save Project As", File::getSpecialLocation(File::userDocumentsDirectory),
      "*.fpc");

  chooser->launchAsync(
      FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles,
      [this, chooser](const FileChooser &fc) {
        auto file = fc.getResult();
        if (file == File())
          return;

        // Ensure .fpc extension
        if (!file.hasFileExtension(".fpc"))
          file = file.withFileExtension(".fpc");

        auto data = gatherProjectData();
        if (ProjectSerializer::saveProject(file, data)) {
          currentProjectFile = file;
          projectModified = false;
        } else {
          AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
                                           "Save Failed",
                                           "Could not save the project file.");
        }
      });
}

void MainComponent::loadProject() {
  auto chooser = std::make_shared<FileChooser>(
      "Load Project", File::getSpecialLocation(File::userDocumentsDirectory),
      "*.fpc");

  chooser->launchAsync(
      FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
      [this, chooser](const FileChooser &fc) {
        auto file = fc.getResult();
        if (file == File() || !file.existsAsFile())
          return;

        ProjectSerializer::ProjectData data;
        if (ProjectSerializer::loadProject(file, data)) {
          applyProjectData(data);
          currentProjectFile = file;
          projectModified = false;
        } else {
          AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
                                           "Load Failed",
                                           "Could not load the project file.");
        }
      });
}

ProjectSerializer::ProjectData MainComponent::gatherProjectData() {
  ProjectSerializer::ProjectData data;

  data.midiFiles = midiFiles;
  data.numVariations = numVariations;
  data.bpm = bpm;

  if (gridComponent != nullptr) {
    // Gather row data
    for (int i = 0; i < numVariations; ++i) {
      auto rowData = gridComponent->getRowData(i);
      ProjectSerializer::RowSettings row;
      row.name = rowData.name;
      row.pluginDesc = rowData.pluginDescription;
      row.pluginState = rowData.pluginState;
      row.volumeDb = rowData.volumeDb;
      data.rows.add(row);
    }

    // Gather column data
    for (int i = 0; i < midiFiles.size(); ++i) {
      auto colSettings = gridComponent->getColumnSettings(i);
      ProjectSerializer::ColumnSettings col;
      col.pitchOffset = colSettings.pitchOffset;
      col.velocityMultiplier = colSettings.velocityMultiplier;
      data.columns.add(col);
    }
  }

  return data;
}

void MainComponent::applyProjectData(
    const ProjectSerializer::ProjectData &data) {
  midiFiles = data.midiFiles;
  numVariations = data.numVariations;
  bpm = data.bpm;

  // Update config panel
  configPanel.setNumVariations(numVariations);
  configPanel.setBpm(bpm);

  // Rebuild grid
  rebuildGrid();

  // Apply row settings (plugins and volume) - this needs to be done after grid
  // rebuild Note: Full plugin state restoration would require more complex
  // implementation For now, the grid is rebuilt with the correct data
}
