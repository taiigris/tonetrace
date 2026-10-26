// Turns the drawn notes into MIDI notes (project beats, snap quantisation, fixed velocity).
#pragma once
#include <string>
#include <vector>

#include "core/NoteEdit.h"
#include "core/Types.h"
#include "midi/MidiFileWriter.h"

namespace ttr {

// Beats are counted from the beat below the earliest note; the Snap grid quantises start and length.
std::vector<MidiNote> planExport(const std::vector<NoteEvent>& notes, const TimeGrid& grid);
// The selected notes, or every note when none is selected.
std::vector<NoteEvent> notesForExport(const std::vector<NoteEvent>& notes);
constexpr double kBeatsPerBar = 4.0;  // exported clips are 4/4
// Length of the clip in beats: the end of the last note rounded up to whole bars (at least one bar).
double clipLengthBeats(const std::vector<MidiNote>& notes);
// The file carries the tempo, a track name and an end-of-track event on a bar line (the clip loops cleanly in the DAW).
std::vector<uint8_t> buildMidiForNotes(const Snapshot& snap, int snapMode, const KeyResult& key);
// "Tonetrace <Key> <BPM>bpm.mid", e.g. "Tonetrace A minor 120bpm.mid" ("Unknown" without a key).
std::string midiFileName(const KeyResult& key, double bpm);
// Same name without the extension, used as the MIDI track name.
std::string midiTrackName(const KeyResult& key);

}  // namespace ttr
