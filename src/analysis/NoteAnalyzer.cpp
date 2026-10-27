#include "analysis/NoteAnalyzer.h"

#include <algorithm>
#include <cmath>

#include "analysis/DrumFilter.h"
#include "analysis/SpectralFrames.h"

namespace ttr {

namespace {

constexpr float kPickFloorDb = -50.0f;
constexpr float kMinResidualShare = 0.25f;  // drop notes mostly explained by lower notes' harmonics
constexpr int kMaxPicksPerFrame = 12;
constexpr int kMedianWidth = 5;
constexpr int kMaxSuppressedHarmonic = 52;
constexpr double kBridgeGapSec = 0.040;
constexpr float kEdgeTrimRatio = 0.35f;
constexpr double kShortNoteSec = 0.15;   // short AND weak segments are almost always partial leakage
constexpr float kWeakStrength = 0.6f;

// Salience harmonics (semitone offsets / weights, spec: harmonics 1..6).
constexpr int kSalienceSemis[6] = {0, 12, 19, 24, 28, 31};
constexpr float kSalienceWeights[6] = {1.0f, 0.5f, 0.33f, 0.25f, 0.2f, 0.16f};

struct Harmonic {
    int semis;
    float share;  // expected amplitude relative to the fundamental (1/h)
};

// Harmonics 2..kMaxSuppressedHarmonic (covers the whole note range for a 41 Hz fundamental). Partials that fall between two notes (> 25 cent off) are subtracted from both neighbours.
std::vector<Harmonic> makeSuppressionHarmonics(float subtractGain) {
    std::vector<Harmonic> out;
    for (int h = 2; h <= kMaxSuppressedHarmonic; ++h) {
        const double semis = 12.0 * std::log2(double(h));
        const int nearest = int(std::lround(semis));
        out.push_back({nearest, subtractGain / h});
        const double deviation = semis - nearest;
        if (std::fabs(deviation) > 0.25) out.push_back({nearest + (deviation > 0 ? 1 : -1), subtractGain / h});
    }
    return out;
}

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

// Greedy pick of fundamentals in one frame; writes residual amplitudes of picked notes to `out`.
void pickFrame(const float* raw, float floorAmp, const std::vector<Harmonic>& suppress, const PickOptions& options, float* out) {
    const float explainFactor = options.explainFactor;
    float residual[kNumNotes];
    bool picked[kNumNotes] = {false};
    std::copy(raw, raw + kNumNotes, residual);
    std::fill(out, out + kNumNotes, 0.0f);

    for (int iter = 0; iter < kMaxPicksPerFrame; ++iter) {
        int best = -1;
        float bestSalience = 0.0f;
        for (int n = 0; n < kNumNotes; ++n) {
            if (picked[n] || residual[n] < floorAmp || residual[n] < kMinResidualShare * raw[n]) continue;
            const bool isPeak = (n == 0 || raw[n] >= raw[n - 1]) && (n == kNumNotes - 1 || raw[n] >= raw[n + 1]);
            if (!isPeak) continue;
            float s = 0.0f;
            for (int h = 0; h < 6; ++h) {
                const int idx = n + kSalienceSemis[h];
                if (idx < kNumNotes) s += kSalienceWeights[h] * residual[idx];
            }
            if (s > bestSalience) { bestSalience = s; best = n; }
        }
        if (best < 0) break;
        const float amp = residual[best];
        out[best] = amp;
        picked[best] = true;
        residual[best] = 0.0f;
        for (const Harmonic& hm : suppress) {
            const int idx = best + hm.semis;
            if (idx >= kNumNotes) continue;
            const float predicted = amp * hm.share;
            // within explainFactor of the prediction the partial is just that overtone (no other note adds to it)
            residual[idx] = residual[idx] <= explainFactor * predicted ? 0.0f : std::max(0.0f, residual[idx] - predicted);
        }
    }
}

void medianFilterTime(std::vector<float>& values, int frames) {
    std::vector<float> src = values;
    const int half = kMedianWidth / 2;
    for (int n = 0; n < kNumNotes; ++n) {
        for (int f = 0; f < frames; ++f) {
            float win[kMedianWidth];
            for (int k = 0; k < kMedianWidth; ++k) {
                const int idx = std::min(frames - 1, std::max(0, f + k - half));
                win[k] = src[size_t(idx) * kNumNotes + n];
            }
            std::sort(win, win + kMedianWidth);
            values[size_t(f) * kNumNotes + n] = win[half];
        }
    }
}

}  // namespace

float sensitivityToThresholdDb(float sensitivity) { return -15.0f - 30.0f * clampf(sensitivity, 0.0f, 1.0f); }

namespace {

constexpr int kProgressStride = 128;

std::shared_ptr<Salience> salienceFromEnergies(const std::vector<float>& energies, int frames, double hopSec, double lengthSec, TaskControl* control) {
    std::vector<float> picks = pickFundamentals(energies, frames, control);
    if (picks.empty()) return nullptr;
    auto result = std::make_shared<Salience>();
    result->frames = frames;
    result->hopSec = hopSec;
    result->lengthSec = lengthSec;
    result->values = std::move(picks);
    const float maxPick = *std::max_element(result->values.begin(), result->values.end());
    for (float& v : result->values) v /= maxPick;
    medianFilterTime(result->values, frames);
    return result;
}

}  // namespace

std::vector<float> pickFundamentals(const std::vector<float>& energies, int frames, TaskControl* control, const PickOptions& options) {
    if (energies.empty()) return {};
    const float maxEnergy = *std::max_element(energies.begin(), energies.end());
    if (maxEnergy <= 1e-9f) return {};
    const float floorAmp = maxEnergy * std::pow(10.0f, kPickFloorDb / 20.0f);
    const std::vector<Harmonic> suppress = makeSuppressionHarmonics(options.subtractGain);
    std::vector<float> values(size_t(frames) * kNumNotes, 0.0f);
    for (int f = 0; f < frames; ++f) {
        if (control && f % kProgressStride == 0) {
            if (control->cancelled()) return {};
            control->report(0.3f + 0.6f * float(f) / float(frames));
        }
        pickFrame(&energies[size_t(f) * kNumNotes], floorAmp, suppress, options, &values[size_t(f) * kNumNotes]);
    }
    const float maxPick = *std::max_element(values.begin(), values.end());
    return maxPick > 0.0f ? values : std::vector<float>();
}

std::shared_ptr<Salience> computeSalience(const Spectrogram& spec, const FilterSettings& filter, TaskControl* control) {
    if (spec.frames == 0) return nullptr;
    if (control) control->report(0.05f);
    if (!filter.hideDrums) return salienceFromEnergies(spec.raw, spec.frames, spec.hopSec, spec.lengthSec, control);
    return salienceFromEnergies(gatedHarmonicEnergies(spec, filter.drumFilter), spec.frames, spec.hopSec, spec.lengthSec, control);
}

namespace {

struct Run { int first, last; };

std::vector<Run> findRuns(const std::vector<char>& active, int bridgeFrames) {
    std::vector<Run> runs;
    const int frames = int(active.size());
    int f = 0;
    while (f < frames) {
        if (!active[f]) { ++f; continue; }
        int last = f;
        int g = f + 1;
        while (g < frames) {
            if (active[g]) { last = g; ++g; continue; }
            int gap = 0;
            while (g + gap < frames && !active[g + gap]) ++gap;
            if (gap > bridgeFrames || g + gap >= frames) break;
            g += gap;
        }
        runs.push_back({f, last});
        f = last + 1;
    }
    return runs;
}

float strengthFromRelative(float meanRel) {
    const float db = 20.0f * std::log10(std::max(meanRel, 1e-4f));
    return clampf((db + 40.0f) / 40.0f, 0.05f, 1.0f);
}

}  // namespace

std::vector<NoteSegment> extractSegments(const Salience& sal, float sensitivity, double minNoteSec) {
    std::vector<NoteSegment> segments;
    if (sal.frames == 0) return segments;
    const float threshold = std::pow(10.0f, sensitivityToThresholdDb(sensitivity) / 20.0f);
    const int bridge = int(kBridgeGapSec / sal.hopSec);
    std::vector<char> active(size_t(sal.frames));

    for (int n = 0; n < kNumNotes; ++n) {
        for (int f = 0; f < sal.frames; ++f) active[size_t(f)] = sal.at(f, n) >= threshold;
        for (Run run : findRuns(active, bridge)) {
            float peak = 0.0f;
            for (int f = run.first; f <= run.last; ++f) peak = std::max(peak, sal.at(f, n));
            while (run.first < run.last && sal.at(run.first, n) < kEdgeTrimRatio * peak) ++run.first;
            while (run.last > run.first && sal.at(run.last, n) < kEdgeTrimRatio * peak) --run.last;

            NoteEvent note;
            note.pitch = kMinNote + n;
            note.startSec = std::max(0.0, run.first * sal.hopSec - sal.hopSec / 2);
            note.endSec = std::min(sal.lengthSec, run.last * sal.hopSec + sal.hopSec / 2);
            if (note.endSec - note.startSec < minNoteSec) continue;
            float sum = 0.0f;
            for (int f = run.first; f <= run.last; ++f) sum += sal.at(f, n);
            note.strength = strengthFromRelative(sum / float(run.last - run.first + 1));
            const bool shortAndWeak = note.endSec - note.startSec < kShortNoteSec && note.strength < kWeakStrength;
            if (!shortAndWeak) segments.push_back({note, n, run.first, run.last});
        }
    }
    return segments;
}

namespace {
void sortNotes(std::vector<NoteEvent>& notes) {
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.startSec != b.startSec ? a.startSec < b.startSec : a.pitch < b.pitch;
    });
}
}  // namespace

std::vector<NoteEvent> extractNotes(const Salience& sal, float sensitivity, const Spectrogram& spec,
                                    const FilterSettings& filter, double minNoteSec) {
    std::vector<NoteEvent> notes;
    if (spec.frames != sal.frames) return notes;
    for (NoteSegment& seg : extractSegments(sal, sensitivity, minNoteSec)) {
        annotateSegment(spec, &seg);
        if (!filter.hideDrums || !isDrumSegment(spec, seg, filter.drumFilter)) notes.push_back(seg.note);
    }
    sortNotes(notes);
    return notes;
}

}  // namespace ttr
