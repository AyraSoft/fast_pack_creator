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
    filterShortMidiFiles(folder); // Remove MIDI files < 4 bars
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
  masterVolume.setValue(-6.0, dontSendNotification);
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
void MainComponent::runBatchNormalization(const File &outputDir) {
  // Find FFmpeg
  String ffmpegPath = "ffmpeg";
  if (File("/usr/local/bin/ffmpeg").existsAsFile())
    ffmpegPath = "/usr/local/bin/ffmpeg";
  else if (File("/opt/homebrew/bin/ffmpeg").existsAsFile())
    ffmpegPath = "/opt/homebrew/bin/ffmpeg";
  else {
    AlertWindow::showMessageBoxAsync(
        MessageBoxIconType::WarningIcon, "FFmpeg Not Found",
        "FFmpeg is required for LUFS normalization.\n\n"
        "Install with: brew install ffmpeg\n\n"
        "Files were rendered but NOT normalized.");
    return;
  }

  // Get normalization settings
  double targetLufs = configPanel.getNormalizationHeadroom();
  int sampleRate = 44100; // Default sample rate
  int bitDepth = 24;      // Default bit depth
  String codec = (bitDepth == 24) ? "pcm_s24le" : "pcm_s16le";

  String loudnormFilter =
      "loudnorm=I=" + String(targetLufs, 1) + ":TP=-1.0:LRA=11";

  DBG("Starting batch normalization: " + String(targetLufs, 1) + " LUFS");

  // Find all WAV files recursively in output directory
  Array<File> wavFiles;
  for (const auto &entry :
       RangedDirectoryIterator(outputDir, true, "*.wav", File::findFiles)) {
    wavFiles.add(entry.getFile());
  }

  DBG("Found " + String(wavFiles.size()) + " WAV files to normalize");

  int normalized = 0;
  int failed = 0;

  for (const auto &wavFile : wavFiles) {
    File tempFile = wavFile.getSiblingFile(
        wavFile.getFileNameWithoutExtension() + "_norm_temp.wav");

    StringArray ffmpegArgs;
    ffmpegArgs.add(ffmpegPath);
    ffmpegArgs.add("-y");
    ffmpegArgs.add("-i");
    ffmpegArgs.add(wavFile.getFullPathName());
    ffmpegArgs.add("-af");
    ffmpegArgs.add(loudnormFilter);
    ffmpegArgs.add("-ar");
    ffmpegArgs.add(String(sampleRate));
    ffmpegArgs.add("-c:a");
    ffmpegArgs.add(codec);
    ffmpegArgs.add(tempFile.getFullPathName());

    ChildProcess ffmpeg;
    if (ffmpeg.start(ffmpegArgs)) {
      ffmpeg.waitForProcessToFinish(30000); // 30 sec timeout per file

      if (ffmpeg.getExitCode() == 0 && tempFile.existsAsFile()) {
        wavFile.deleteFile();
        tempFile.moveFileTo(wavFile);
        normalized++;
      } else {
        tempFile.deleteFile();
        failed++;
        DBG("Failed to normalize: " + wavFile.getFileName());
      }
    } else {
      failed++;
    }
  }

  DBG("Normalization complete: " + String(normalized) + " OK, " +
      String(failed) + " failed");

  if (failed > 0) {
    AlertWindow::showMessageBoxAsync(
        MessageBoxIconType::WarningIcon, "Normalization Partial",
        "Normalized " + String(normalized) + " files.\n" + String(failed) +
            " files failed to normalize.");
  }
}

//==============================================================================
void MainComponent::filterShortMidiFiles(const File &folder) {
  // Check if midicsv is installed
  String midicsvPath = "midicsv";
  if (File("/usr/local/bin/midicsv").existsAsFile())
    midicsvPath = "/usr/local/bin/midicsv";
  else if (File("/opt/homebrew/bin/midicsv").existsAsFile())
    midicsvPath = "/opt/homebrew/bin/midicsv";
  else {
    // Check if midicsv is available at all
    ChildProcess checkMidicsv;
    if (!checkMidicsv.start("which midicsv")) {
      AlertWindow::showMessageBoxAsync(
          MessageBoxIconType::WarningIcon, "midicsv Not Found",
          "midicsv is required to filter short MIDI files.\n\n"
          "Install with: brew install midicsv\n\n"
          "MIDI files will be loaded without filtering.");
      return;
    }
    checkMidicsv.waitForProcessToFinish(5000);
    if (checkMidicsv.getExitCode() != 0) {
      AlertWindow::showMessageBoxAsync(
          MessageBoxIconType::WarningIcon, "midicsv Not Found",
          "midicsv is required to filter short MIDI files.\n\n"
          "Install with: brew install midicsv\n\n"
          "MIDI files will be loaded without filtering.");
      return;
    }
  }

  DBG("Using midicsv at: " + midicsvPath);

  // Find all MIDI files
  Array<File> filesToCheck;
  for (const auto &entry : RangedDirectoryIterator(
           folder, false, "*.mid;*.midi", File::findFiles)) {
    filesToCheck.add(entry.getFile());
  }

  StringArray filesToDelete;

  for (const auto &midiFile : filesToCheck) {
    // Run midicsv on the file
    StringArray midicsvArgs;
    midicsvArgs.add(midicsvPath);
    midicsvArgs.add(midiFile.getFullPathName());

    ChildProcess midicsv;
    if (midicsv.start(midicsvArgs)) {
      String output = midicsv.readAllProcessOutput();
      midicsv.waitForProcessToFinish(10000);

      // Parse PPQ from Header line (6th field)
      int ppq = 0;
      int lastTick = 0;

      StringArray lines = StringArray::fromLines(output);
      for (const auto &line : lines) {
        if (line.contains("Header")) {
          // Format: track, time, Header, format, nTracks, division
          StringArray parts = StringArray::fromTokens(line, ",", "");
          if (parts.size() >= 6)
            ppq = parts[5].trim().getIntValue();
        }

        // Get last tick (second field of each line)
        StringArray parts = StringArray::fromTokens(line, ",", "");
        if (parts.size() >= 2) {
          int tick = parts[1].trim().getIntValue();
          if (tick > lastTick)
            lastTick = tick;
        }
      }

      // Check if duration is less than 4 bars (16 beats = ppq * 16)
      if (ppq > 0 && lastTick > 0) {
        int threshold = ppq * 16; // 4 bars in 4/4
        if (lastTick < threshold) {
          filesToDelete.add(midiFile.getFileName());
          midiFile.deleteFile();
          DBG("Deleted short MIDI: " + midiFile.getFileName() +
              " (ticks: " + String(lastTick) + " < " + String(threshold) + ")");
        }
      }
    }
  }

  // Show summary if files were deleted
  if (!filesToDelete.isEmpty()) {
    String message = "Removed " + String(filesToDelete.size()) +
                     " MIDI file(s) shorter than 4 bars:\n\n";
    for (int i = 0; i < jmin(10, filesToDelete.size()); ++i)
      message += "• " + filesToDelete[i] + "\n";
    if (filesToDelete.size() > 10)
      message += "...and " + String(filesToDelete.size() - 10) + " more.";

    AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon,
                                     "MIDI Files Filtered", message);
  }
}

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
    currentOutputDir = chooser.getResult();

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
    processNextRenderPass(currentOutputDir);
  }
}

void MainComponent::processNextRenderPass(const File &outputDir) {
  if (renderQueue.empty()) {
    // All passes complete
    // Restore state
    bpm = initialBpm;
    pluginHost->setBpm(bpm);
    configPanel.setBpm(bpm);

    // Run batch normalization if enabled
    bool normalizeEnabled = configPanel.isNormalizationEnabled();
    if (normalizeEnabled) {
      runBatchNormalization(outputDir);
    }

    // Get list of problematic files
    StringArray problemFiles = parallelRenderer
                                   ? parallelRenderer->getProblematicFiles()
                                   : StringArray();

    MessageManager::callAsync([this, normalizeEnabled, problemFiles] {
      if (progressWindow) {
        progressWindow->setVisible(false);
        progressWindow.reset();
      }

      String message;
      MessageBoxIconType icon = MessageBoxIconType::InfoIcon;
      String title = "Rendering Complete";

      if (!problemFiles.isEmpty()) {
        icon = MessageBoxIconType::WarningIcon;
        title = "Rendering Complete - Issues Found";
        message = "The following files may have problems:\n\n";
        for (auto &file : problemFiles)
          message += "• " + file + "\n";
        message += "\nPlease check these files manually.";
      } else if (normalizeEnabled) {
        message = "All files rendered and normalized successfully!";
      } else {
        message = "All files have been rendered successfully!";
      }

      AlertWindow::showMessageBoxAsync(icon, title, message);
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
  settings.masterGainDb = static_cast<float>(masterVolume.getValue());
  settings.loop = configPanel.isLoopEnabled();
  settings.seamlessLoop = configPanel.isSeamlessLoopEnabled();
  settings.normalize = configPanel.isNormalizationEnabled();
  settings.normalizationLufs = configPanel.getNormalizationHeadroom();

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
      job.bpm =
          bpm; // BPM for this job (variation BPM for tempo-synced plugins)

      // File naming: add [Loop] or [Trail] suffix based on mode
      String modeSuffix = settings.loop ? " [Loop]" : " [Trail]";

      File midiDir =
          outputDir.getChildFile(midiFile.getFileNameWithoutExtension());
      midiDir.createDirectory();

      String filename = midiFile.getFileNameWithoutExtension() + " [" +
                        rowData.name + "]" + " [" + String(bpm) + " BPM]" +
                        modeSuffix + ".wav";

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
      parallelRenderer.reset(); // Clean up current renderer

      // Continue with next render pass (if any)
      processNextRenderPass(currentOutputDir);
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
