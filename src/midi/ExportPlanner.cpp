#include "midi/ExportPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "analysis/KeyEstimator.h"
#include "core/Instruments.h"

namespace ttr {

namespace {

// Melodic channels (0-based) in the order they are handed out; channel 10 (index 9) stays the drums'.
constexpr int kMelodicChannels[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15};
constexpr size_t kMelodicCount = sizeof(kMelodicChannels) / sizeof(kMelodicChannels[0]);
constexpr size_t kReuseFrom = 9;  // past 15 instruments the last six channels are shared

int melodicChannel(size_t index) {
    if (index < kMelodicCount) return kMelodicChannels[index];
    return kMelodicChannels[kReuseFrom + (index - kMelodicCount) % (kMelodicCount - kReuseFrom)];
}

std::string keyLabel(const KeyResult& key) { return key.root < 0 ? "Unknown" : keyName(key); }

std::string tempoLabel(double bpm) {
    char tempo[32];
    if (std::fabs(bpm - std::round(bpm)) < 0.05) snprintf(tempo, sizeof tempo, "%.0f", bpm);
    else snprintf(tempo, sizeof tempo, "%.1f", bpm);
    return tempo;
}

std::string songStem(const std::string& fileName) {
    const size_t slash = fileName.find_last_of('/');
    std::string stem = slash == std::string::npos ? fileName : fileName.substr(slash + 1);
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0) stem.resize(dot);
    return stem;
}

}  // namespace

std::vector<MidiNote> planExport(const std::vector<NoteEvent>& notes, const TimeGrid& grid, bool fromStart) {
    std::vector<MidiNote> out;
    if (notes.empty()) return out;
    const double bps = grid.beatsPerSec();
    const double step = snapGridBeats(grid.snapMode);
    std::vector<MidiNote> beats;
    beats.reserve(notes.size());
    for (const NoteEvent& n : notes) {
        double start = grid.startPpq + n.startSec * bps;
        double length = (n.endSec - n.startSec) * bps;
        if (step > 0.0) {
            start = std::round(start / step) * step;
            length = std::max(step, std::round(length / step) * step);
        }
        MidiNote m{n.pitch, start, std::max(length, 0.01), kExportVelocity};
        m.program = n.program;
        beats.push_back(m);
    }
    double first = beats.front().startBeat;
    for (const MidiNote& n : beats) first = std::min(first, n.startBeat);
    const double origin = fromStart ? std::floor(grid.startPpq + 1e-9) : std::floor(first + 1e-9);
    for (MidiNote n : beats) {
        n.startBeat = std::max(0.0, n.startBeat - origin);
        out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch;
    });
    return out;
}

std::vector<MidiTrack> planTracks(const std::vector<MidiNote>& notes) {
    std::map<int, std::vector<MidiNote>> byProgram;  // ascending: kNoProgram first, drums last
    for (const MidiNote& n : notes) byProgram[n.program].push_back(n);
    std::vector<MidiTrack> tracks;
    size_t melodic = 0;
    for (auto& [program, group] : byProgram) {
        MidiTrack t;
        t.name = instrumentName(program);
        const bool isDrums = program == kDrumProgram;
        t.channel = isDrums ? kDrumChannel : melodicChannel(melodic++);
        t.program = isDrums ? 0 : program;  // on channel 10 the program picks the kit: 0 = standard
        for (MidiNote& n : group) n.channel = t.channel;
        t.notes = std::move(group);
        tracks.push_back(std::move(t));
    }
    return tracks;
}

std::vector<NoteEvent> notesForExport(const std::vector<NoteEvent>& notes) {
    std::vector<NoteEvent> picked;
    for (const NoteEvent& n : notes) if (n.selected) picked.push_back(n);
    return picked.empty() ? notes : picked;
}

double clipLengthBeats(const std::vector<MidiNote>& notes) {
    double end = 0.0;
    for (const MidiNote& n : notes) end = std::max(end, n.startBeat + n.lengthBeat);
    return std::max(1.0, std::ceil(end / kBeatsPerBar - 1e-9)) * kBeatsPerBar;
}

std::vector<uint8_t> buildMidiForNotes(const Snapshot& snap, int snapMode, const KeyResult& key) {
    TimeGrid grid;
    grid.bpm = snap.bpm;
    grid.startPpq = snap.startPpq;
    grid.snapMode = snapMode;
    const std::vector<NoteEvent> picked = notesForExport(snap.notes);
    const bool isSong = !snap.sourceName.empty();
    const std::vector<MidiNote> notes = planExport(picked, grid, isSong);
    if (!hasInstruments(picked)) return writeMidiFile(notes, snap.bpm, midiTrackName(key), clipLengthBeats(notes));
    const std::string title = isSong ? songStem(snap.sourceName) : midiTrackName(key);
    return writeMidiTracks(planTracks(notes), snap.bpm, title, clipLengthBeats(notes));
}

std::string midiTrackName(const KeyResult& key) { return "Tonetrace " + keyLabel(key); }

std::string midiFileName(const KeyResult& key, double bpm) { return midiTrackName(key) + " " + tempoLabel(bpm) + "bpm.mid"; }

std::string midiFileNameFor(const Snapshot& snap) {
    if (snap.sourceName.empty()) return midiFileName(snap.key, snap.bpm);
    return songStem(snap.sourceName) + " Tonetrace " + tempoLabel(snap.bpm) + "bpm.mid";
}

}  // namespace ttr
