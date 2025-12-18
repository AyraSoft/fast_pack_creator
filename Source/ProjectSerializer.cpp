/*
  ==============================================================================

    ProjectSerializer.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "ProjectSerializer.h"

const char *const ProjectSerializer::projectFileExtension = ".fpc";
const char *const ProjectSerializer::projectTagName = "FastPackCreatorProject";

//==============================================================================
bool ProjectSerializer::saveProject(const File &file, const ProjectData &data) {
  auto xml = toXml(data);
  if (xml == nullptr)
    return false;

  return xml->writeTo(file);
}

bool ProjectSerializer::loadProject(const File &file, ProjectData &data) {
  auto xml = XmlDocument::parse(file);
  if (xml == nullptr)
    return false;

  return fromXml(*xml, data);
}

//==============================================================================
std::unique_ptr<XmlElement> ProjectSerializer::toXml(const ProjectData &data) {
  auto xml = std::make_unique<XmlElement>(projectTagName);

  // Settings
  xml->setAttribute("numVariations", data.numVariations);
  xml->setAttribute("bpm", data.bpm);

  // MIDI Files
  auto midiFilesXml = xml->createNewChildElement("MidiFiles");
  for (auto &file : data.midiFiles) {
    auto fileXml = midiFilesXml->createNewChildElement("File");
    fileXml->setAttribute("path", file.getFullPathName());
  }

  // Rows
  auto rowsXml = xml->createNewChildElement("Rows");
  for (auto &row : data.rows) {
    auto rowXml = rowsXml->createNewChildElement("Row");
    rowXml->setAttribute("name", row.name);
    rowXml->setAttribute("volumeDb", row.volumeDb);

    // Plugin description
    if (row.pluginDesc.name.isNotEmpty()) {
      auto pluginXml = row.pluginDesc.createXml();
      if (pluginXml != nullptr) {
        rowXml->addChildElement(pluginXml.release());
      }
    }

    // Plugin state
    if (row.pluginState.getSize() > 0) {
      auto stateXml = rowXml->createNewChildElement("PluginState");
      stateXml->addTextElement(row.pluginState.toBase64Encoding());
    }
  }

  // Columns
  auto columnsXml = xml->createNewChildElement("Columns");
  for (auto &col : data.columns) {
    auto colXml = columnsXml->createNewChildElement("Column");
    colXml->setAttribute("pitchOffset", col.pitchOffset);
    colXml->setAttribute("velocityMultiplier", col.velocityMultiplier);
  }

  return xml;
}

bool ProjectSerializer::fromXml(const XmlElement &xml, ProjectData &data) {
  if (xml.getTagName() != projectTagName)
    return false;

  // Settings
  data.numVariations = xml.getIntAttribute("numVariations", 10);
  data.bpm = xml.getDoubleAttribute("bpm", 120.0);

  // MIDI Files
  data.midiFiles.clear();
  if (auto midiFilesXml = xml.getChildByName("MidiFiles")) {
    for (auto *fileXml : midiFilesXml->getChildIterator()) {
      if (fileXml->hasTagName("File")) {
        File file(fileXml->getStringAttribute("path"));
        if (file.existsAsFile()) {
          data.midiFiles.add(file);
        }
      }
    }
  }

  // Rows
  data.rows.clear();
  if (auto rowsXml = xml.getChildByName("Rows")) {
    for (auto *rowXml : rowsXml->getChildIterator()) {
      if (rowXml->hasTagName("Row")) {
        RowSettings row;
        row.name = rowXml->getStringAttribute("name");
        row.volumeDb =
            static_cast<float>(rowXml->getDoubleAttribute("volumeDb", 0.0));

        // Plugin description
        if (auto pluginXml = rowXml->getChildByName("PLUGIN")) {
          row.pluginDesc.loadFromXml(*pluginXml);
        }

        // Plugin state
        if (auto stateXml = rowXml->getChildByName("PluginState")) {
          row.pluginState.fromBase64Encoding(stateXml->getAllSubText());
        }

        data.rows.add(row);
      }
    }
  }

  // Columns
  data.columns.clear();
  if (auto columnsXml = xml.getChildByName("Columns")) {
    for (auto *colXml : columnsXml->getChildIterator()) {
      if (colXml->hasTagName("Column")) {
        ColumnSettings col;
        col.pitchOffset = colXml->getIntAttribute("pitchOffset", 0);
        col.velocityMultiplier = static_cast<float>(
            colXml->getDoubleAttribute("velocityMultiplier", 1.0));
        data.columns.add(col);
      }
    }
  }

  return true;
}
