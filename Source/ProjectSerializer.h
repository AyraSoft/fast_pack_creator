/*
  ==============================================================================

    ProjectSerializer.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

//==============================================================================
class ProjectSerializer {
public:
  //==============================================================================
  struct RowSettings {
    String name;
    PluginDescription pluginDesc;
    MemoryBlock pluginState;
    float volumeDb = 0.0f;
  };

  struct ColumnSettings {
    int pitchOffset = 0;
    float velocityMultiplier = 1.0f;
  };

  struct ProjectData {
    Array<File> midiFiles;
    int numVariations = 10;
    double bpm = 120.0;
    Array<RowSettings> rows;
    Array<ColumnSettings> columns;
  };

  //==============================================================================
  static bool saveProject(const File &file, const ProjectData &data);
  static bool loadProject(const File &file, ProjectData &data);

  static std::unique_ptr<XmlElement> toXml(const ProjectData &data);
  static bool fromXml(const XmlElement &xml, ProjectData &data);

private:
  static const char *const projectFileExtension;
  static const char *const projectTagName;
};
