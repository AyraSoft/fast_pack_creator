/*
  ==============================================================================

    MidiGridComponent.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Refactored to use TableListBox for stability

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "MidiGridComponent.h"

//==============================================================================
MidiGridComponent::MidiGridComponent(ayra::PluginsManager &pm, PluginHost &host)
    : pluginsManager(pm), pluginHost(host) {
  // Setup row header viewport
  rowHeaderViewport.setViewedComponent(&rowHeaderContainer, false);
  rowHeaderViewport.setScrollBarsShown(false, false);
  addAndMakeVisible(rowHeaderViewport);

  // Setup column header viewport (for custom headers with pitch/velocity)
  columnHeaderViewport.setViewedComponent(&columnHeaderContainer, false);
  columnHeaderViewport.setScrollBarsShown(false, false);
  addAndMakeVisible(columnHeaderViewport);

  // Setup table (disable built-in header since we use custom)
  table.setModel(this);
  table.setHeaderHeight(0); // Disable built-in header
  table.setRowHeight(ROW_HEIGHT);
  table.getViewport()->getVerticalScrollBar().addListener(this);
  table.getViewport()->getHorizontalScrollBar().addListener(this);
  addAndMakeVisible(table);

  // Setup corner button for macro operations
  loadAllPluginsButton.onClick = [this] { loadPluginToAllRows(); };
  addAndMakeVisible(loadAllPluginsButton);

  openCloseAllPluginsGuiButton.onClick = [this] { openCloseAllPluginsGui(); };
  addAndMakeVisible(openCloseAllPluginsGuiButton);

  renderizableAllOnButton.onClick = [this] { renderizableAllOn(); };
  addAndMakeVisible(renderizableAllOnButton);

  renderizableAllOffButton.onClick = [this] { renderizableAllOff(); };
  addAndMakeVisible(renderizableAllOffButton);
}

MidiGridComponent::~MidiGridComponent() {
  table.setModel(nullptr);

  if (auto *viewport = table.getViewport()) {
    viewport->getVerticalScrollBar().removeListener(this);
    viewport->getHorizontalScrollBar().removeListener(this);
  }
}

//==============================================================================
void MidiGridComponent::paint(Graphics &g) {
  g.fillAll(getLookAndFeel().findColour(ResizableWindow::backgroundColourId));
}

void MidiGridComponent::resized() {
  auto bounds = getLocalBounds();

  // Column headers at top (right of row header corner)
  auto columnHeaderArea = bounds.removeFromTop(COLUMN_HEADER_HEIGHT);
  auto cornerArea = columnHeaderArea.removeFromLeft(ROW_HEADER_WIDTH);
  auto cornerAreaHalf = cornerArea.removeFromTop(cornerArea.getHeight() / 2);
  loadAllPluginsButton.setBounds(
      cornerAreaHalf.removeFromLeft(cornerAreaHalf.getWidth() / 2).reduced(5));
  openCloseAllPluginsGuiButton.setBounds(cornerAreaHalf.reduced(5));
  renderizableAllOnButton.setBounds(
      cornerArea.removeFromLeft(cornerArea.getWidth() / 2).reduced(5));
  renderizableAllOffButton.setBounds(cornerArea.reduced(5));
  columnHeaderViewport.setBounds(columnHeaderArea);

  // Resize column header container to fit all headers
  int totalColWidth = midiFiles.size() * COLUMN_WIDTH;
  columnHeaderContainer.setBounds(0, 0, totalColWidth, COLUMN_HEADER_HEIGHT);

  // Row headers on left (below the corner)
  auto rowHeaderArea = bounds.removeFromLeft(ROW_HEADER_WIDTH);
  rowHeaderViewport.setBounds(rowHeaderArea);

  // Resize row header container to fit all headers
  rowHeaderContainer.setBounds(0, 0, ROW_HEADER_WIDTH,
                               numVariations * ROW_HEIGHT);

  // Table takes remaining space
  table.setBounds(bounds);
}

//==============================================================================
void MidiGridComponent::setMidiFiles(const Array<File> &files) {
  midiFiles = files;
}

void MidiGridComponent::setNumVariations(int num) { numVariations = num; }

void MidiGridComponent::setBpm(double newBpm) {
  bpm = newBpm;
  pluginHost.setBpm(newBpm);
}

void MidiGridComponent::rebuild() {
  rebuildRowHeaders();
  rebuildTable();
  resized();
}

// ... existing code ...

//==============================================================================
MidiMessageSequence MidiGridComponent::loadMidiFile(const File &file) {
  MidiMessageSequence result;

  FileInputStream stream(file);
  if (stream.openedOk()) {
    MidiFile midiFile;
    if (midiFile.readFrom(stream)) {
      // Convert timestamps from Ticks to Beats (PPQ)
      // Standard MIDI file time format: positive = ticks per quarter note
      double ticksPerQuarterNote = 960.0;
      int timeFormat = midiFile.getTimeFormat();
      if (timeFormat > 0)
        ticksPerQuarterNote = timeFormat;

      for (int track = 0; track < midiFile.getNumTracks(); ++track) {
        auto *trackSeq = midiFile.getTrack(track);
        for (int i = 0; i < trackSeq->getNumEvents(); ++i) {
          auto msg = trackSeq->getEventPointer(i)->message;
          // Convert ticks to beats
          msg.setTimeStamp(msg.getTimeStamp() / ticksPerQuarterNote);
          result.addEvent(msg);
        }
      }
    }
  }

  result.updateMatchedPairs();
  return result;
}

MidiMessageSequence MidiGridComponent::applyTransformations(
    const MidiMessageSequence &seq, int pitchOffset, float velocityMultiplier) {
  MidiMessageSequence result;

  for (int i = 0; i < seq.getNumEvents(); ++i) {
    auto *event = seq.getEventPointer(i);
    auto msg = event->message;

    if (msg.isNoteOnOrOff()) {
      int newNote = jlimit(0, 127, msg.getNoteNumber() + pitchOffset);
      float newVel =
          jlimit(0.0f, 1.0f, msg.getFloatVelocity() * velocityMultiplier);

      if (msg.isNoteOn())
        msg = MidiMessage::noteOn(msg.getChannel(), newNote, newVel);
      else
        msg = MidiMessage::noteOff(msg.getChannel(), newNote);

      msg.setTimeStamp(event->message.getTimeStamp());
    }

    result.addEvent(msg);
  }

  result.updateMatchedPairs();
  return result;
}

void MidiGridComponent::loadPluginToAllRows() {
  if (rowHeaders.size() == 0)
    return;

  PopupMenu menu;
  pluginsManager.addPluginsToMenu(menu);

  menu.showMenuAsync(
      PopupMenu::Options().withTargetComponent(&loadAllPluginsButton),
      [this](int result) {
        if (result > 0) {
          auto desc = pluginsManager.getChosenType(result);
          int loadedCount = 0;

          for (auto *header : rowHeaders) {
            String errorMessage;
            auto plugin = pluginsManager.createPluginInstance(
                desc, 44100.0, 2048, errorMessage);

            if (plugin != nullptr) {
              header->setPlugin(std::move(plugin), desc.pluginDescription);
              loadedCount++;
            }
          }

          AlertWindow::showMessageBoxAsync(
              MessageBoxIconType::InfoIcon, "Load All",
              "Plugin loaded to " + String(loadedCount) + " of " +
                  String(rowHeaders.size()) + " rows");
        }
      });
}

void MidiGridComponent::openCloseAllPluginsGui() {
  bool guiFound = false;

  for (const auto header : rowHeaders) {
    if (header->isPluginEditorShown()) {
      guiFound = true;
      break;
    }
  }

  for (const auto header : rowHeaders)
    guiFound ? header->closePluginEditor() : header->showPluginEditor();
}

// Old implementations removed

//==============================================================================
MidiGridComponent::ColumnSettings
MidiGridComponent::getColumnSettings(int columnIndex) const {
  if (columnIndex >= 0 && columnIndex < columnHeaders.size()) {
    return {columnHeaders[columnIndex]->getPitchOffset(),
            columnHeaders[columnIndex]->getVelocityMultiplier()};
  }
  return {};
}

MidiGridComponent::RowData MidiGridComponent::getRowData(int rowIndex) const {
  if (rowIndex >= 0 && rowIndex < rowHeaders.size()) {
    RowData data;
    data.name = rowHeaders[rowIndex]->getVariationName();
    data.pluginDescription = rowHeaders[rowIndex]->getPluginDescription();
    data.pluginState = rowHeaders[rowIndex]->getPluginState();
    data.volumeDb = rowHeaders[rowIndex]->getVolumeDb();
    return data;
  }
  return {};
}

//==============================================================================
void MidiGridComponent::rebuildRowHeaders() {
  rowHeaders.clear();
  rowHeaderContainer.removeAllChildren();

  for (int row = 0; row < numVariations; ++row) {
    auto *header = new RowHeader(row, pluginsManager);
    header->onSelected = [this, row] { handleRowSelection(row); };

    // Connect macro toggle to toggle all cells in this row
    header->onMacroToggle = [this, row] { toggleRowRenderizable(row); };

    // Handle volume change
    header->onVolumeChanged = [this, row](float db) {
      // Update plugin host if this row's plugin is currently active?
      // Actually, handleCellPlay sets the gain when playback starts.
      // But if we change volume WHILE playing, we should update it.
      // We can check if pluginHost active plugin matches this row's plugin.
      auto *plugin = rowHeaders[row]->getPlugin();
      if (plugin != nullptr && pluginHost.getActivePlugin() == plugin) {
        pluginHost.setGain(Decibels::decibelsToGain(db));
      }
    };

    header->setBounds(0, row * ROW_HEIGHT, ROW_HEADER_WIDTH, ROW_HEIGHT);
    rowHeaders.add(header);
    rowHeaderContainer.addAndMakeVisible(header);
  }

  rowHeaderContainer.setSize(ROW_HEADER_WIDTH, numVariations * ROW_HEIGHT);
}

void MidiGridComponent::rebuildTable() {
  // Clear existing columns
  table.getHeader().removeAllColumns();
  columnHeaders.clear();
  columnHeaderContainer.removeAllChildren();
  // cells.clear(); // REMOVED

  // Resize state vector
  cellRenderizableState.resize(numVariations);
  for (int i = 0; i < numVariations; ++i) {
    cellRenderizableState[i].resize(midiFiles.size(), true); // Default true
  }

  // Add columns for each MIDI file
  for (int col = 0; col < midiFiles.size(); ++col) {
    auto &file = midiFiles.getReference(col);

    // Create custom column header component with pitch/velocity sliders
    auto *header = new ColumnHeader(col, file);
    header->setBounds(col * COLUMN_WIDTH, 0, COLUMN_WIDTH,
                      COLUMN_HEADER_HEIGHT);

    // Connect macro toggle to toggle all cells in this column
    header->onMacroToggle = [this, col] { toggleColumnRenderizable(col); };

    columnHeaders.add(header);
    columnHeaderContainer.addAndMakeVisible(header);

    // Add column to table (invisible header since height=0)
    table.getHeader().addColumn(file.getFileNameWithoutExtension(),
                                col + 1, // columnId (1-based)
                                COLUMN_WIDTH, 50, 200,
                                TableHeaderComponent::notResizableOrSortable);
  }

  // Update column container size
  columnHeaderContainer.setSize(midiFiles.size() * COLUMN_WIDTH,
                                COLUMN_HEADER_HEIGHT);

  table.updateContent();
}

//==============================================================================
// Old toggle implementations removed

//==============================================================================
// TableListBoxModel implementation

int MidiGridComponent::getNumRows() { return numVariations; }

void MidiGridComponent::paintRowBackground(Graphics &g, int rowNumber,
                                           int width, int height,
                                           bool rowIsSelected) {
  auto colour = getLookAndFeel().findColour(ListBox::backgroundColourId);
  if (rowIsSelected)
    colour = colour.brighter(0.2f);
  else if (rowNumber % 2 == 1)
    colour = colour.darker(0.05f);

  g.fillAll(colour);
}

void MidiGridComponent::paintCell(Graphics &g, int rowNumber, int columnId,
                                  int width, int height, bool rowIsSelected) {
  // Cells are rendered via refreshComponentForCell
}

Component *MidiGridComponent::refreshComponentForCell(
    int rowNumber, int columnId, bool isRowSelected,
    Component *existingComponentToUpdate) {

  int columnIndex = columnId - 1; // Convert to 0-based

  if (columnIndex < 0 || columnIndex >= midiFiles.size())
    return nullptr;

  auto *cell = static_cast<CellPad *>(existingComponentToUpdate);

  if (cell == nullptr) {
    cell = new CellPad(rowNumber, columnIndex);
    // Play/Stop handlers...
    cell->onPlay = [this](int r, int c) { handleCellPlay(r, c); };
    cell->onStop = [this](int r, int c) { handleCellStop(r, c); };

    // Renderizable toggle handler
    cell->renderizable.onClick = [this, cell] {
      onCellRenderizableChanged(cell->getRow(), cell->getColumn(),
                                cell->isRenderizable());
    };

    // cells.add(cell); // REMOVED
  } else {
    // Update existing cell if row/column changed
    cell->setRowAndColumn(rowNumber, columnIndex);
  }

  // Update cell UI from state
  if (rowNumber >= 0 && rowNumber < cellRenderizableState.size()) {
    if (columnIndex >= 0 &&
        columnIndex < cellRenderizableState[rowNumber].size()) {
      cell->setRenderizable(cellRenderizableState[rowNumber][columnIndex]);
    }
  }

  return cell;
}

//==============================================================================
void MidiGridComponent::scrollBarMoved(ScrollBar *scrollBarThatHasMoved,
                                       double newRangeStart) {
  if (scrollBarThatHasMoved == &table.getViewport()->getVerticalScrollBar()) {
    rowHeaderViewport.setViewPosition(0, static_cast<int>(newRangeStart));
  } else if (scrollBarThatHasMoved ==
             &table.getViewport()->getHorizontalScrollBar()) {
    columnHeaderViewport.setViewPosition(static_cast<int>(newRangeStart), 0);
  }
}

//==============================================================================
bool MidiGridComponent::isCellRenderizable(int row, int column) const {
  if (row >= 0 && row < cellRenderizableState.size()) {
    if (column >= 0 && column < cellRenderizableState[row].size()) {
      return cellRenderizableState[row][column];
    }
  }
  return false;
}

void MidiGridComponent::setCellRenderizable(int row, int column,
                                            bool shouldBeRenderizable) {
  if (row >= 0 && row < cellRenderizableState.size()) {
    if (column >= 0 && column < cellRenderizableState[row].size()) {
      cellRenderizableState[row][column] = shouldBeRenderizable;
      // We should repaint that cell if visible, or just repaint table
      table.repaint();
    }
  }
}

void MidiGridComponent::onCellRenderizableChanged(int row, int column,
                                                  bool newState) {
  // Update model from UI interaction
  if (row >= 0 && row < cellRenderizableState.size()) {
    if (column >= 0 && column < cellRenderizableState[row].size()) {
      cellRenderizableState[row][column] = newState;
    }
  }
}

void MidiGridComponent::toggleRowRenderizable(int rowIndex) {
  if (rowIndex >= 0 && rowIndex < cellRenderizableState.size()) {
    // Check first one to decide toggle direction
    bool anyOn = false;
    for (bool val : cellRenderizableState[rowIndex]) {
      if (val) {
        anyOn = true;
        break;
      }
    }

    bool newState =
        !anyOn; // If any is On, turn all Off? Or if all Off turn On?
    // Standard toggle: if all are Same, flip. If mixed, set to On.
    // Let's simplified: If all are OFF, set ON. Else set OFF.

    // Better: Toggle based on state?
    // Let's just flip all? No, that's chaotic.
    // Set all to !first?
    if (cellRenderizableState[rowIndex].size() > 0)
      newState = !cellRenderizableState[rowIndex][0];

    for (size_t i = 0; i < cellRenderizableState[rowIndex].size(); ++i)
      cellRenderizableState[rowIndex][i] = newState;

    table.repaint();
  }
}

void MidiGridComponent::toggleColumnRenderizable(int columnIndex) {
  for (auto &row : cellRenderizableState) {
    if (columnIndex >= 0 && columnIndex < row.size())
      row[columnIndex] = !row[columnIndex];
  }
  table.repaint();
}

void MidiGridComponent::renderizableAllOn() {
  for (auto &row : cellRenderizableState)
    std::fill(row.begin(), row.end(), true);
  table.repaint();
}

void MidiGridComponent::renderizableAllOff() {
  for (auto &row : cellRenderizableState)
    std::fill(row.begin(), row.end(), false);
  table.repaint();
}

// REMOVED getCellAt implementation

//==============================================================================
void MidiGridComponent::handleCellPlay(int row, int column) {
  if (row < 0 || row >= rowHeaders.size() || column < 0 ||
      column >= midiFiles.size())
    return;

  auto *rowHeader = rowHeaders[row];
  auto *plugin = rowHeader->getPlugin();

  if (plugin == nullptr) {
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon,
                                     "No Plugin",
                                     "Please load a plugin for this row first");
    return;
  }

  // Load and play MIDI
  auto midiSequence = loadMidiFile(midiFiles[column]);
  auto columnSettings = getColumnSettings(column);
  auto transformedSequence =
      applyTransformations(midiSequence, columnSettings.pitchOffset,
                           columnSettings.velocityMultiplier);

  // Set active plugin and play with volume
  pluginHost.setActivePlugin(plugin);
  pluginHost.setGain(Decibels::decibelsToGain(rowHeader->getVolumeDb()));
  pluginHost.playMidiSequence(transformedSequence, bpm);
}

void MidiGridComponent::handleCellStop(int row, int column) {
  pluginHost.stopPlayback();
}

void MidiGridComponent::handleRowSelection(int rowIndex) {
  if (selectedRowIndex >= 0 && selectedRowIndex < rowHeaders.size())
    rowHeaders[selectedRowIndex]->setSelected(false);

  selectedRowIndex = rowIndex;

  if (selectedRowIndex >= 0 && selectedRowIndex < rowHeaders.size()) {
    auto *header = rowHeaders[selectedRowIndex];
    header->setSelected(true);

    // Set as active plugin for MIDI input
    pluginHost.setActivePlugin(header->getPlugin());
    // Also update gain/volume for live input monitoring?
    pluginHost.setGain(Decibels::decibelsToGain(header->getVolumeDb()));
  }
}
