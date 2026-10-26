#include "core/Snapshot.h"

#include <algorithm>

#include "analysis/KeyEstimator.h"
#include "analysis/NoteAnalyzer.h"

namespace ttr {

std::array<float, 12> spectrogramChroma(const Spectrogram& s) {
    std::array<float, 12> chroma{};
    const std::shared_ptr<Salience> salience = computeSalience(s, FilterSettings{});
    if (!salience) return chroma;
    for (int f = 0; f < salience->frames; ++f)
        for (int n = 0; n < kNumNotes; ++n) chroma[size_t((kMinNote + n) % 12)] += salience->at(f, n);
    const float peak = *std::max_element(chroma.begin(), chroma.end());
    if (peak > 0.0f) for (float& v : chroma) v /= peak;
    return chroma;
}

KeyResult keyOf(const std::vector<NoteEvent>& notes, const std::array<float, 12>& chroma) {
    return estimateKey(notes.empty() ? chroma : pitchClassWeights(notes));
}

SnapshotPtr makeSnapshot(std::vector<NoteEvent> notes, const Recording& rec, uint64_t version) {
    auto s = std::make_shared<Snapshot>();
    s->spectrogram = rec.spectrogram;
    s->peaks = rec.peaks;
    s->audio = rec.audio;
    s->chroma = rec.chroma;
    s->lengthSec = rec.lengthSec;
    s->bpm = rec.bpm;
    s->startPpq = rec.startPpq;
    s->key = keyOf(notes, s->chroma);
    s->notes = std::move(notes);
    s->version = version;
    return s;
}

SnapshotPtr withNotes(const Snapshot& old, std::vector<NoteEvent> notes) {
    auto s = std::make_shared<Snapshot>(old);
    s->key = keyOf(notes, s->chroma);
    s->notes = std::move(notes);
    s->version = old.version + 1;
    return s;
}

SnapshotPtr withRecording(const Snapshot* old, const Recording& rec) {
    return makeSnapshot(old ? old->notes : std::vector<NoteEvent>{}, rec, old ? old->version + 1 : 1);
}

}  // namespace ttr
