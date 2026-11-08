#include "midi/ExportPlanner.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

#include "core/Instruments.h"

namespace ttr {

namespace {

// Melodic channels (0-based) in the order they are handed out; channel 10 (index 9) stays the drums'.
constexpr int kMelodicChannels[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15};
constexpr size_t kMelodicCount = sizeof(kMelodicChannels) / sizeof(kMelodicChannels[0]);
constexpr size_t kReuseFrom = 9;  // past 15 instruments the last six channels are shared
constexpr double kMinLengthBeats = 0.01;

int melodicChannel(size_t index) {
    if (index < kMelodicCount) return kMelodicChannels[index];
    return kMelodicChannels[kReuseFrom + (index - kMelodicCount) % (kMelodicCount - kReuseFrom)];
}

std::string tempoLabel(double bpm) {
    char tempo[32];
    if (std::fabs(bpm - std::round(bpm)) < 0.05) snprintf(tempo, sizeof tempo, "%.0f", bpm);
    else snprintf(tempo, sizeof tempo, "%.1f", bpm);
    return tempo;
}

}  // namespace

std::vector<MidiNote> planExport(const std::vector<NoteEvent>& notes, double bpm) {
    const double beatsPerSec = (bpm > 0.0 && std::isfinite(bpm) ? bpm : kDefaultBpm) / 60.0;
    std::vector<MidiNote> out;
    out.reserve(notes.size());
    for (const NoteEvent& n : notes) {
        MidiNote m{n.pitch, std::max(0.0, n.startSec) * beatsPerSec, std::max((n.endSec - n.startSec) * beatsPerSec, kMinLengthBeats), kExportVelocity};
        m.program = n.program;
        out.push_back(m);
    }
    std::sort(out.begin(), out.end(), [](const MidiNote& a, const MidiNote& b) {
        return a.startBeat != b.startBeat ? a.startBeat < b.startBeat : a.pitch < b.pitch;
    });
    return out;
}

std::vector<MidiTrack> planTracks(const std::vector<MidiNote>& notes) {
    std::map<int, std::vector<MidiNote>> byProgram;  // ascending: drums (128) last
    for (const MidiNote& n : notes) byProgram[n.program].push_back(n);
    std::vector<MidiTrack> tracks;
    size_t melodic = 0;
    for (auto& [program, group] : byProgram) {
        MidiTrack t;
        t.name = instrumentName(program);
        t.instrument = gmSoundName(program);
        const bool isDrums = program == kDrumProgram;
        t.channel = isDrums ? kDrumChannel : melodicChannel(melodic++);
        t.program = isDrums ? 0 : program;  // on channel 10 the program picks the kit: 0 = standard
        for (MidiNote& n : group) n.channel = t.channel;
        t.notes = std::move(group);
        tracks.push_back(std::move(t));
    }
    return tracks;
}

double clipLengthBeats(const std::vector<MidiNote>& notes) {
    double end = 0.0;
    for (const MidiNote& n : notes) end = std::max(end, n.startBeat + n.lengthBeat);
    return std::max(1.0, std::ceil(end / kBeatsPerBar - 1e-9)) * kBeatsPerBar;
}

std::vector<NoteEvent> notesForExport(const std::vector<NoteEvent>& notes, const MixerState& mixer, bool allInstruments) {
    if (allInstruments) return notes;
    std::vector<NoteEvent> picked;
    picked.reserve(notes.size());
    for (const NoteEvent& n : notes)
        if (mixer.isAudible(n.program)) picked.push_back(n);
    return picked;
}

std::vector<uint8_t> buildMidi(const std::vector<NoteEvent>& notes, double bpm, const std::string& title) {
    const std::vector<MidiNote> planned = planExport(notes, bpm);
    return writeMidiTracks(planTracks(planned), bpm, title, clipLengthBeats(planned));
}

std::string songStem(const std::string& sourceName) {
    std::string stem = sourceName.empty() ? std::string("Tonetrace") : sourceName;
    const size_t slash = stem.find_last_of('/');
    if (slash != std::string::npos) stem = stem.substr(slash + 1);
    const size_t dot = stem.find_last_of('.');
    if (dot != std::string::npos && dot > 0) stem.resize(dot);
    return stem;
}

std::string midiFileName(const std::string& sourceName, double bpm) { return songStem(sourceName) + " Tonetrace " + tempoLabel(bpm) + "bpm.mid"; }

}  // namespace ttr
