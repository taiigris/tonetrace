#include "midi/ExportPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "analysis/KeyEstimator.h"

namespace ttr {

std::vector<MidiNote> planExport(const std::vector<NoteEvent>& notes, const TimeGrid& grid) {
    std::vector<MidiNote> out;
    if (notes.empty()) return out;
    const double bps = grid.beatsPerSec();
    const double step = snapGridBeats(grid.snapMode);
    std::vector<MidiNote> beats;
    for (const NoteEvent& n : notes) {
        double start = grid.startPpq + n.startSec * bps;
        double length = (n.endSec - n.startSec) * bps;
        if (step > 0.0) {
            start = std::round(start / step) * step;
            length = std::max(step, std::round(length / step) * step);
        }
        beats.push_back({n.pitch, start, std::max(length, 0.01), kExportVelocity});
    }
    double first = beats.front().startBeat;
    for (const MidiNote& n : beats) first = std::min(first, n.startBeat);
    const double origin = std::floor(first + 1e-9);
    for (MidiNote n : beats) {
        n.startBeat = std::max(0.0, n.startBeat - origin);
        out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch;
    });
    return out;
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
    const std::vector<MidiNote> notes = planExport(notesForExport(snap.notes), grid);
    return writeMidiFile(notes, snap.bpm, midiTrackName(key), clipLengthBeats(notes));
}

namespace {
std::string keyLabel(const KeyResult& key) { return key.root < 0 ? "Unknown" : keyName(key); }
}  // namespace

std::string midiTrackName(const KeyResult& key) { return "Tonetrace " + keyLabel(key); }

std::string midiFileName(const KeyResult& key, double bpm) {
    char tempo[32];
    if (std::fabs(bpm - std::round(bpm)) < 0.05) snprintf(tempo, sizeof tempo, "%.0f", bpm);
    else snprintf(tempo, sizeof tempo, "%.1f", bpm);
    return midiTrackName(key) + " " + tempo + "bpm.mid";
}

}  // namespace ttr
