// Turns the transcribed notes into a MIDI file: seconds -> beats at the export tempo (from the song start), one track per
// instrument (NeuralNote-style), drums on channel 10.
#pragma once
#include <string>
#include <vector>

#include "core/Mixer.h"
#include "core/Types.h"
#include "midi/MidiFileWriter.h"

namespace ttr {

constexpr double kBeatsPerBar = 4.0;  // exported files are 4/4
constexpr int kDrumChannel = 9;       // 0-based: MIDI channel 10

// Notes in beats at `bpm`, counted from the song start; each keeps its program.
std::vector<MidiNote> planExport(const std::vector<NoteEvent>& notes, double bpm);
// One track per instrument, ascending by program (drums last). Drums play on channel 10 (GM percussion); the others get
// channels 1-9 and 11-16 in that order, later ones share the last six channels.
std::vector<MidiTrack> planTracks(const std::vector<MidiNote>& notes);
// Length of the file in beats: the end of the last note rounded up to whole bars (at least one bar).
double clipLengthBeats(const std::vector<MidiNote>& notes);
// The notes of the instruments `mixer` lets through (every note when `allInstruments`).
std::vector<NoteEvent> notesForExport(const std::vector<NoteEvent>& notes, const MixerState& mixer, bool allInstruments);
// The whole file; `title` names the tempo track.
std::vector<uint8_t> buildMidi(const std::vector<NoteEvent>& notes, double bpm, const std::string& title);
// "<song> Tonetrace <BPM>bpm.mid" (the song's file name without its extension).
std::string midiFileName(const std::string& sourceName, double bpm);
std::string songStem(const std::string& sourceName);

}  // namespace ttr
