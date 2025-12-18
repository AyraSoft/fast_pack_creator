/*
  ==============================================================================

    MidiPlayer.cpp
    Fast Pack Creator - MIDI Batch Renderer

    Created: 2024
    Author:  Federico De Biase / Ayra Soft

  ==============================================================================
*/

#include "MidiPlayer.h"

MidiMessageSequence MidiPlayer::loadMidiFile(const File &file, double bpm) {
  MidiMessageSequence result;

  FileInputStream stream(file);
  if (stream.openedOk()) {
    MidiFile midiFile;
    if (midiFile.readFrom(stream)) {
      midiFile.convertTimestampTicksToSeconds();
      for (int track = 0; track < midiFile.getNumTracks(); ++track) {
        result.addSequence(*midiFile.getTrack(track), 0.0);
      }
    }
  }

  result.updateMatchedPairs();
  return result;
}

MidiMessageSequence
MidiPlayer::applyTransformations(const MidiMessageSequence &seq,
                                 int pitchOffset, float velocityMultiplier) {
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

double MidiPlayer::getSequenceDuration(const MidiMessageSequence &seq) {
  return seq.getEndTime();
}
