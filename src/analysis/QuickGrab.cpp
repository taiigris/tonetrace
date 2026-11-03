#include "analysis/QuickGrab.h"

#include <algorithm>
#include <cmath>

#include "analysis/NoteAnalyzer.h"
#include "analysis/LowBand.h"
#include "analysis/SpectralFrames.h"
#include "analysis/SustainGrab.h"
#include "analysis/ml/NeuralEngine.h"

namespace ttr {

std::vector<NoteEvent> quickGrabNotes(const Spectrogram& spectrogram, float amount, const FilterSettings& filter, TaskControl* control) {
    const std::shared_ptr<Salience> salience = computeSalience(spectrogram, filter, control);
    if (!salience || (control && control->cancelled())) return {};
    std::vector<NoteEvent> notes = extractNotes(*salience, amount, spectrogram, filter, kQuickGrabMinNoteSec);
    if (control) control->report(1.0f);
    for (NoteEvent& n : notes) { n.strength = 1.0f; n.selected = false; }
    return notes;
}

std::vector<NoteEvent> quickGrabFromLines(const PitchLines& lines, float amount, double tuningCents, const FilterSettings& filter) {
    constexpr double kMinGrabSec = 0.06;
    std::vector<NoteEvent> notes;
    const float threshold = lines.peak * std::pow(10.0f, sensitivityToThresholdDb(amount) / 20.0f);
    for (const LineTrack& t : lines.tracks) {
        if (t.flags & (kLineHidden | kLineHarmonic)) continue;
        if (filter.hideDrums && (t.flags & kLineDrum)) continue;
        if (t.peakLevel < threshold || t.endSec - t.startSec < kMinGrabSec) continue;
        NoteEvent n;
        n.pitch = std::clamp(rowOfMidi(t.meanMidi, tuningCents), kMinNote, kMaxNote);
        n.startSec = std::max(0.0, t.startSec);
        n.endSec = t.endSec;
        notes.push_back(n);
    }
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.startSec != b.startSec ? a.startSec < b.startSec : a.pitch < b.pitch;
    });
    return notes;
}

std::vector<NoteEvent> quickGrabSnapshot(const Snapshot& snapshot, float amount, const FilterSettings& filter, double tuningCents, TaskControl* control) {
    // 4.4: held notes (SustainGrab); the Hide drums toggle only dims the picture, the fine pitch (tuning) only shifts the display grid.
    if (!snapshot.spectrogram) return {};
    const Spectrogram& spec = *snapshot.spectrogram;
    std::vector<float> low;
    if (spec.low.empty() && snapshot.audio && !snapshot.audio->samples.empty()) {
        std::vector<float> samples(snapshot.audio->samples.size());
        for (size_t i = 0; i < samples.size(); ++i) samples[i] = float(snapshot.audio->samples[i]) / 32767.0f;
        low = computeLowEnergies(samples.data(), long(samples.size()), snapshot.audio->sampleRate, spec.frames, kHopSize);
    }
    std::vector<NoteEvent> notes = sustainGrabNotes(spec, spec.low.empty() ? low : spec.low, amount, {snapshot.bpm, snapshot.startPpq}, control);
    for (NoteEvent& n : notes) { n.strength = 1.0f; n.selected = false; }
    return notes;
}

GrabOutcome quickGrabWithEngine(const Snapshot& snapshot, float amount, const FilterSettings& filter, double tuningCents, GrabEngine engine,
                                TaskControl* control) {
    GrabOutcome outcome;
    if (engine == GrabEngine::Neural) {
        NeuralGrab neural = neuralGrabSnapshot(snapshot, amount, filter, control);
        if (neural.ok) {
            outcome.notes = std::move(neural.notes);
            outcome.used = GrabEngine::Neural;
            return outcome;
        }
        if (control && control->cancelled()) return outcome;
        outcome.fallbackReason = neural.reason;
    }
    outcome.notes = quickGrabSnapshot(snapshot, amount, filter, tuningCents, control);
    return outcome;
}

}  // namespace ttr
