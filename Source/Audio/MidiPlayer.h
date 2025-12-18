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
};
