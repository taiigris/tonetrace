// Minimal Standard MIDI File (type 1, 960 PPQ) writer and parser (parser is used by tests).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ttr {

constexpr int kMidiPpq = 960;

struct MidiNote {
    int pitch = 60;
    double startBeat = 0.0;
    double lengthBeat = 1.0;
    int velocity = 100;
};

// clipBeats > 0 places the end-of-track event there (never before the last note-off), so a host that takes the
// clip length from it gets a loop of whole bars.
std::vector<uint8_t> writeMidiFile(const std::vector<MidiNote>& notes, double bpm, const std::string& trackName, double clipBeats = 0.0);

struct ParsedMidi {
    bool valid = false;
    int format = 0;
    int tracks = 0;
    int ppq = 0;
    double bpm = 0.0;
    double endBeat = 0.0;  // end-of-track position of the note track
    std::string trackName;
    std::vector<MidiNote> notes;  // sorted by start
};

ParsedMidi parseMidiFile(const std::vector<uint8_t>& bytes);

}  // namespace ttr
