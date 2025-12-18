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

double MidiPlayer::getMidiFileDuration(const File &file, double bpm) {
  FileInputStream stream(file);
  if (!stream.openedOk()) {
    return 0.0;
  }

  MidiFile midiFile;
  if (!midiFile.readFrom(stream)) {
    return 0.0;
  }

  // Get the MIDI file's tick-based length
  short timeFormat = midiFile.getTimeFormat(); // Ticks per quarter note (PPQ)
  if (timeFormat <= 0) {
    // SMPTE format not supported
    return 0.0;
  }

  // Find the last NOTE-ON event tick (ignoring note-offs)
  // This tells us when the last note STARTS, not when it ends
  int lastNoteOnTick = 0;
  for (int track = 0; track < midiFile.getNumTracks(); ++track) {
    const MidiMessageSequence *trackSeq = midiFile.getTrack(track);
    if (trackSeq != nullptr) {
      for (int i = 0; i < trackSeq->getNumEvents(); ++i) {
        auto *event = trackSeq->getEventPointer(i);
        if (event != nullptr && event->message.isNoteOn()) {
          int eventTick = static_cast<int>(event->message.getTimeStamp());
          if (eventTick > lastNoteOnTick) {
            lastNoteOnTick = eventTick;
          }
        }
      }
    }
  }

  // Convert ticks to beats (quarter notes)
  double beats =
      static_cast<double>(lastNoteOnTick) / static_cast<double>(timeFormat);

  // Calculate which bar the last note is in (1-indexed)
  // A note at beat 0-3.99 is in bar 1, beat 4-7.99 is in bar 2, etc.
  double beatsPerBar = 4.0; // Assuming 4/4 time signature

  // Find the bar number (1-indexed) that contains this beat
  // floor(beats / beatsPerBar) gives us the 0-indexed bar number
  // We want to truncate at the END of that bar
  int barNumber = static_cast<int>(
      beats / beatsPerBar); // 0-indexed bar containing the note
  double roundedBars = static_cast<double>(barNumber + 1); // Complete this bar

  // If note is exactly on a bar boundary (beat 4, 8, 12, etc.),
  // it's the FIRST note of that bar, so we include it
  double roundedBeats = roundedBars * beatsPerBar;

  DBG("getMidiFileDuration: lastNoteOnTick="
      << lastNoteOnTick << " PPQ=" << timeFormat << " beats=" << beats
      << " barNumber=" << barNumber << " roundedBars=" << roundedBars
      << " roundedBeats=" << roundedBeats);

  // Ensure minimum of 1 bar
  if (roundedBeats < beatsPerBar)
    roundedBeats = beatsPerBar;

  // Convert beats to seconds using BPM
  double secondsPerBeat = 60.0 / bpm;
  double durationSeconds = roundedBeats * secondsPerBeat;

  DBG("getMidiFileDuration: BPM=" << bpm << " secondsPerBeat=" << secondsPerBeat
                                  << " durationSeconds=" << durationSeconds);

  return durationSeconds;
}
