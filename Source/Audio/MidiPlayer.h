/*
  ==============================================================================

    MidiPlayer.h
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

class MidiPlayer {
public:
  static MidiMessageSequence loadMidiFile(const File &file, double bpm);

  static MidiMessageSequence
  applyTransformations(const MidiMessageSequence &seq, int pitchOffset,
                       float velocityMultiplier);

  static double getSequenceDuration(const MidiMessageSequence &seq);

  // Get the MIDI file duration in seconds based on actual file length (for loop
  // mode)
  static double getMidiFileDuration(const File &file, double bpm);
};
