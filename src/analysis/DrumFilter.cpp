#include "analysis/DrumFilter.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {

// Gate: onset strength = ramp(percFrac) * ramp(flux); thresholds move with the drum-filter amount.
constexpr float kPercLoAtZero = 0.10f, kPercLoAtOne = 0.0f, kPercRamp = 0.08f;
constexpr float kFluxLoAtZero = 0.45f, kFluxLoAtOne = 0.10f, kFluxRamp = 0.25f;
constexpr float kGateOnsetMin = 0.35f;
constexpr int kNoHistoryFrames = 2;
constexpr double kGateBeforeSec = 0.045;  // analysis windows see a hit before it happens
constexpr double kGateAfterSec = 0.060;

// Segment filter.
constexpr float kTonalityMinAtZero = 0.10f, kTonalityMinAtOne = 0.60f;
constexpr double kShortSec = 0.120;
constexpr float kLowSustainAtZero = 0.15f, kLowSustainAtOne = 0.60f;
constexpr double kGlideLookSec = 0.080;
constexpr float kGlideSemitones = 3.0f;
constexpr int kGlideMinSteps = 2;
constexpr double kGlideMaxSec = 0.600;
// Drum tail: a decaying note that begins shortly after a percussive onset (kick body, tom, rim ring).
constexpr double kTailLookSec = 0.35, kTailMaxSec = 0.600;
constexpr float kTailSustainAtZero = 0.60f, kTailSustainAtOne = 0.90f;
constexpr float kTailOnsetMin = 0.5f;
// Short blips right at a hit (kick sweeping through the bass notes, snare leakage).
constexpr double kBlipMaxSec = 0.150, kBlipNearSec = 0.100;

float lerp(float a, float b, float t) { return a + (b - a) * t; }
float ramp(float v, float lo, float width) { return std::min(1.0f, std::max(0.0f, (v - lo) / width)); }

float onsetStrength(const Spectrogram& spec, int frame, float drumFilter) {
    const float percLo = lerp(kPercLoAtZero, kPercLoAtOne, drumFilter), fluxLo = lerp(kFluxLoAtZero, kFluxLoAtOne, drumFilter);
    const float fluxPart = ramp(spec.flux[size_t(frame)], fluxLo, kFluxRamp);
    // The first frames have no history, so everything in them is new: the percussive share is not meaningful there.
    return frame < kNoHistoryFrames ? fluxPart : ramp(spec.percFrac[size_t(frame)], percLo, kPercRamp) * fluxPart;
}

}  // namespace

std::vector<float> gatedHarmonicEnergies(const Spectrogram& spec, float drumFilter) {
    const float d = std::min(1.0f, std::max(0.0f, drumFilter));
    const int frames = spec.frames;
    const int before = int(std::ceil(kGateBeforeSec / spec.hopSec)), after = int(std::ceil(kGateAfterSec / spec.hopSec));
    std::vector<char> gated(size_t(frames), 0);
    for (int f = 0; f < frames; ++f) {
        if (onsetStrength(spec, f, d) < kGateOnsetMin) continue;
        for (int g = std::max(0, f - before); g <= std::min(frames - 1, f + after); ++g) gated[size_t(g)] = 1;
    }

    // Inside a gated stretch no note may be louder than it was just before or just after it: a sustained
    // note carries through untouched, energy that only the hit brings (leakage, tails) is clipped away.
    std::vector<float> out = spec.harmonic;
    for (int a = 0; a < frames;) {
        if (!gated[size_t(a)]) { ++a; continue; }
        int b = a;
        while (b + 1 < frames && gated[size_t(b + 1)]) ++b;
        for (int n = 0; n < kNumNotes; ++n) {
            const float prev = a > 0 ? spec.harmAt(a - 1, n) : 0.0f, next = b + 1 < frames ? spec.harmAt(b + 1, n) : 0.0f;
            const float ref = std::max(prev, next);
            for (int f = a; f <= b; ++f) out[size_t(f) * kNumNotes + size_t(n)] = std::min(out[size_t(f) * kNumNotes + size_t(n)], ref);
        }
        a = b + 1;
    }
    return out;
}

std::vector<char> percussiveFrames(const Spectrogram& spec, float drumFilter, double beforeSec, double afterSec) {
    const float d = std::min(1.0f, std::max(0.0f, drumFilter));
    const int before = int(std::ceil(beforeSec / spec.hopSec)), after = int(std::ceil(afterSec / spec.hopSec));
    std::vector<char> flags(size_t(spec.frames), 0);
    for (int f = 0; f < spec.frames; ++f) {
        if (onsetStrength(spec, f, d) < kGateOnsetMin) continue;
        for (int g = std::max(0, f - before); g <= std::min(spec.frames - 1, f + after); ++g) flags[size_t(g)] = 1;
    }
    return flags;
}

void annotateSegment(const Spectrogram& spec, NoteSegment* seg) {
    double tonal = 0.0, first = 0.0, second = 0.0;
    const int mid = (seg->first + seg->last + 1) / 2;
    for (int f = seg->first; f <= seg->last; ++f) {
        const float raw = spec.rawAt(f, seg->noteIndex), harm = spec.harmAt(f, seg->noteIndex);
        tonal += raw > 0.0f ? std::min(1.0f, harm / raw) : 0.0f;
        (f < mid ? first : second) += harm;
    }
    const int count = seg->last - seg->first + 1;
    seg->note.tonality = float(tonal / count);
    seg->note.sustain = first > 0.0 ? float(std::min(2.0, second / first)) : 1.0f;
}

namespace {

// Strongest note within [n, n + 14] at a frame (raw energy): the pitch a sweeping kick passes through.
int strongestAbove(const Spectrogram& spec, int frame, int n) {
    int best = n;
    for (int m = n; m <= std::min(kNumNotes - 1, n + 14); ++m)
        if (spec.rawAt(frame, m) > spec.rawAt(frame, best)) best = m;
    return best;
}

// Kick-style glide: the strongest pitch above this note slides down into it over the few frames before the
// segment starts (several distinct steps, never rising). A note that simply replaces a previous one shows a
// single jump (its own harmonics vanish) and does not qualify.
bool startsWithDownwardGlide(const Spectrogram& spec, const NoteSegment& seg) {
    const int look = std::max(3, int(std::lround(kGlideLookSec / spec.hopSec)));
    if (seg.first - look < 0) return false;
    int previous = strongestAbove(spec, seg.first - look, seg.noteIndex);
    const int startPitch = previous;
    int steps = 0;
    for (int f = seg.first - look + 1; f <= seg.first; ++f) {
        const int p = strongestAbove(spec, f, seg.noteIndex);
        if (p > previous + 1) return false;  // rising again: not a falling sweep
        steps += p < previous;
        previous = p;
    }
    return steps >= kGlideMinSteps && float(startPitch - previous) >= kGlideSemitones;
}

bool followsPercussiveOnset(const Spectrogram& spec, const NoteSegment& seg, float drumFilter) {
    const int look = int(std::ceil(kTailLookSec / spec.hopSec));
    for (int f = std::max(0, seg.first - look); f <= seg.first; ++f)
        if (onsetStrength(spec, f, drumFilter) >= kTailOnsetMin) return true;
    return false;
}

bool nearPercussiveOnset(const Spectrogram& spec, const NoteSegment& seg, float drumFilter) {
    const int near = int(std::ceil(kBlipNearSec / spec.hopSec));
    for (int f = std::max(0, seg.first - near); f <= std::min(spec.frames - 1, seg.first + near); ++f)
        if (onsetStrength(spec, f, drumFilter) >= kTailOnsetMin) return true;
    return false;
}

}  // namespace

DrumReason drumReason(const Spectrogram& spec, const NoteSegment& seg, float drumFilter) {
    const float d = std::min(1.0f, std::max(0.0f, drumFilter));
    const NoteEvent& n = seg.note;
    const double dur = n.endSec - n.startSec;
    if (n.tonality < lerp(kTonalityMinAtZero, kTonalityMinAtOne, d)) return DrumReason::LowTonality;
    if (dur < kShortSec && n.sustain < lerp(kLowSustainAtZero, kLowSustainAtOne, d)) return DrumReason::ShortDecay;
    if (dur < kBlipMaxSec && nearPercussiveOnset(spec, seg, d)) return DrumReason::BlipAtOnset;
    if (dur < kGlideMaxSec && d > 0.0f && startsWithDownwardGlide(spec, seg)) return DrumReason::DownwardGlide;
    const bool tail = dur < kTailMaxSec && n.sustain < lerp(kTailSustainAtZero, kTailSustainAtOne, d);
    return tail && followsPercussiveOnset(spec, seg, d) ? DrumReason::DrumTail : DrumReason::None;
}

}  // namespace ttr
