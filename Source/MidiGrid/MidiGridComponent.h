/*
  ==============================================================================

    MidiGridComponent.h
    Fast Pack Creator - MIDI Batch Renderer

    Refactored to use TableListBox for stability

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include "../Audio/PluginHost.h"
#include "CellPad.h"
#include "ColumnHeader.h"
#include "RowHeader.h"
#include <JuceHeader.h>

//==============================================================================
class MidiGridComponent : public Component,
                          public TableListBoxModel,
                          public ScrollBar::Listener {
public:
  //==============================================================================
  struct ColumnSettings {
    int pitchOffset = 0;
    float velocityMultiplier = 1.0f;
  };

  struct RowData {
    String name;
    PluginDescription pluginDescription;
    MemoryBlock pluginState;
    float volumeDb = 0.0f;
  };

  //==============================================================================
  MidiGridComponent(ayra::PluginsManager &pm, PluginHost &host);
  ~MidiGridComponent() override;

  //==============================================================================
  void paint(Graphics &) override;
  void resized() override;

  //==============================================================================
  void setMidiFiles(const Array<File> &files);
  void setNumVariations(int numVariations);
  void setBpm(double newBpm);
  void rebuild();

  ColumnSettings getColumnSettings(int columnIndex) const;
  RowData getRowData(int rowIndex) const;

  //==============================================================================
  // TableListBoxModel
  int getNumRows() override;
  void paintRowBackground(Graphics &g, int rowNumber, int width, int height,
                          bool rowIsSelected) override;
  void paintCell(Graphics &g, int rowNumber, int columnId, int width,
                 int height, bool rowIsSelected) override;
  Component *
  refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
                          Component *existingComponentToUpdate) override;

  // ScrollBar::Listener - to sync row headers with table scroll
  void scrollBarMoved(ScrollBar *scrollBarThatHasMoved,
                      double newRangeStart) override;

  // CellPad *getCellAt(int row, int column); // REMOVED - unsafe with
  // virtualization
  bool isCellRenderizable(int row, int column) const;
  void setCellRenderizable(int row, int column, bool shouldBeRenderizable);

  //==============================================================================
  // OSC Remote Control - Public methods for external triggering
  void triggerCellPlay(int row, int column);
  void triggerCellStop(int row, int column);
  void togglePluginGui(int row);
  void openPluginGui(int row);
  void closePluginGui(int row);
  int getNumVariations() const { return numVariations; }
  int getNumColumns() const { return midiFiles.size(); }

private:
  //==============================================================================
  static constexpr int ROW_HEADER_WIDTH = 180;
  static constexpr int ROW_HEIGHT = 80;
  static constexpr int COLUMN_WIDTH = 100;

  //==============================================================================
  ayra::PluginsManager &pluginsManager;
  PluginHost &pluginHost;

  Array<File> midiFiles;
  int numVariations = 10;
  double bpm = 120.0;

  //==============================================================================
  // Row headers (left side)
  OwnedArray<RowHeader> rowHeaders;
  Viewport rowHeaderViewport;
  Component rowHeaderContainer;

  // Column headers (top, with pitch/velocity sliders)
  OwnedArray<ColumnHeader> columnHeaders;
  Viewport columnHeaderViewport;
  Component columnHeaderContainer;
  static constexpr int COLUMN_HEADER_HEIGHT = 80;

  // Main table
  TableListBox table;

  // Corner area controls (top-left, above row headers)
  TextButton loadAllPluginsButton{"Load All"};
  TextButton openCloseAllPluginsGuiButton{"Open/Close All Guis"};
  TextButton renderizableAllOnButton{"Renderizable All On"};
  TextButton renderizableAllOffButton{"Renderizable All Off"};

  // Cells managed by the table
  // OwnedArray<CellPad> cells; // REMOVED to fix double-ownership crash

  // Persistent state for cells (since components are virtualized)
  std::vector<std::vector<bool>> cellRenderizableState;

  int selectedRowIndex = -1;

  //==============================================================================
  void rebuildRowHeaders();
  void rebuildTable();
  void handleCellPlay(int row, int column);
  void handleCellStop(int row, int column);
  void handleRowSelection(int rowIndex);

  MidiMessageSequence loadMidiFile(const File &file);
  MidiMessageSequence applyTransformations(const MidiMessageSequence &seq,
                                           int pitchOffset,
                                           float velocityMultiplier);

  void loadPluginToAllRows();

  void toggleRowRenderizable(int rowIndex);
  void toggleColumnRenderizable(int columnIndex);

  void openCloseAllPluginsGui();
  void renderizableAllOn();
  void renderizableAllOff();

  // Helper for CellPad callbacks
  void onCellRenderizableChanged(int row, int column, bool newState);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiGridComponent)
};
