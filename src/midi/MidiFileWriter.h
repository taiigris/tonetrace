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
    int program = -1;  // instrument of the source note (core kNoProgram); parsed: the channel's last Program Change
    int channel = 0;   // 0..15; parsed: the channel of the note-on
};

// One MIDI track: a name, its channel and (program >= 0) Bank Select (GM bank 0) plus Program Change at the start, so a
// GM synth in the DAW picks the instrument by itself. `instrument` (the GM sound name) becomes an Instrument Name meta event.
struct MidiTrack {
    std::string name;
    std::string instrument;
    int channel = 0;
    int program = -1;
    std::vector<MidiNote> notes;
};

// clipBeats > 0 places the end-of-track event there (never before the last note-off), so a host that takes the
// clip length from it gets a loop of whole bars.
std::vector<uint8_t> writeMidiFile(const std::vector<MidiNote>& notes, double bpm, const std::string& trackName, double clipBeats = 0.0);
// 4.7: type 1 file with a tempo track and one track per entry (NeuralNote-style: one track per instrument, so a DAW
// that splits the import by track gets one MIDI track per instrument). `title` names the tempo track.
std::vector<uint8_t> writeMidiTracks(const std::vector<MidiTrack>& tracks, double bpm, const std::string& title, double clipBeats = 0.0);

struct ParsedMidi {
    bool valid = false;
    int format = 0;
    int tracks = 0;
    int ppq = 0;
    double bpm = 0.0;
    double endBeat = 0.0;  // latest end-of-track position of the note tracks
    std::string trackName;  // name of the first note track
    std::vector<std::string> trackNames;  // every track, the tempo track included
    std::vector<std::string> instrumentNames;  // per track: the Instrument Name meta event (empty when none)
    std::vector<int> trackPrograms;  // per track: the first Program Change (-1 when none)
    std::vector<int> trackBanks;     // per track: the first Bank Select MSB (CC 0; -1 when none)
    bool hasGmSystemOn = false;      // the file carries the GM System On SysEx
    std::vector<MidiNote> notes;  // sorted by start
};

ParsedMidi parseMidiFile(const std::vector<uint8_t>& bytes);

}  // namespace ttr
