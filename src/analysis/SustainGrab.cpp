#include "analysis/SustainGrab.h"

#include <algorithm>
#include <cmath>

#include "analysis/BeatHits.h"
#include "analysis/DrumFilter.h"
#include "analysis/LowBand.h"
#include "analysis/NoteAnalyzer.h"
#include "analysis/SustainRuns.h"

namespace ttr {

namespace {

constexpr float kHysteresisDb = 7.5f;        // T_off = T_on - 7.5 dB
constexpr float kBandFloorDb = -12.0f;       // the local maximum of an octave band is never taken below global - 12 dB
constexpr double kLeakageBinHz = 5.38;       // bin width of the 4096 window at 22.05 kHz
constexpr double kLeakageMaxBins = 6.0;      // a stronger pick this close masks a weak one ...
constexpr float kLeakageRatio = 0.05f;       // ... when it is more than 26 dB weaker
constexpr float kSubtractGain = 1.0f, kExplainFactor = 1.4f;  // overtones: full subtraction, tolerance +40 %
constexpr int kKickTopNote = 48;             // kicks live below C3 ...
constexpr double kKickBlipMaxSec = 0.35;     // ... and sweep through a note for less than this
constexpr int kKickLeadFrames = 3;
constexpr double kKickPulseMaxSec = 1.3;     // below C3 a single beat of pulsing is enough for runs up to this long
constexpr int kGhostSemitones = 7;           // a low note within a fifth of a note this much stronger ...
constexpr float kGhostRatio = 5.0f;          // (14 dB) ...
constexpr double kGhostRingSec = 0.25;
constexpr int kHighGhostNote = 96;           // C7 and above: partials of strong notes beyond the harmonic cancellation
constexpr double kGhostOverlap = 0.7;        // ... that covers it for 70 % of its length is its leakage
constexpr double kKickTailLeadSec = 0.35;
constexpr double kKickTailDecay = 0.8;
constexpr int kProminenceNear = 3, kProminenceFar = 8;  // the notes 3 to 8 semitones around a pitch ...
constexpr double kMinProminence = 1.5;       // ... must be at least 4 dB below it, or the band is noise
constexpr double kNoiseGateMaxSec = 1.0;
constexpr int kSmoothFrames = 5;
constexpr int kHeldFrames = 9;              // ~0.2 s: a band reference needs a level that is held this long
constexpr double kShortBridgeSec = 0.04;     // ordinary gap between two pieces of one pitch
constexpr double kMinBridgeSec = 0.12, kMaxBridgeSec = 0.25;  // gap across drum hits / pump dips: an eighth note, clamped
constexpr double kHitBeforeSec = 0.045, kHitAfterSec = 0.060;
constexpr double kFallbackMinSec = 0.120;
constexpr double kShortNoteSec = 0.15, kWeakUntilSec = 0.30;
constexpr float kWeakShortDb = -16.0f, kWeakMediumDb = -30.0f;
constexpr float kDrumFilter = 0.6f;

float dbToLin(float db) { return std::pow(10.0f, db / 20.0f); }

double eighthNoteSec(double bpm) { return bpm >= 40.0 && bpm <= 300.0 ? 30.0 / bpm : 0.25; }

// Raw (not harmonic-masked) energies, the bass register from the long windows: what a tone is doing underneath drum hits.
std::vector<float> rawEnergies(const Spectrogram& spec, const std::vector<float>& low) {
    std::vector<float> e = spec.raw;
    if (low.size() != size_t(spec.frames) * kLowNotes) return e;
    for (int f = 0; f < spec.frames; ++f)
        for (int n = 0; n < kLowNotes; ++n)
            e[size_t(f) * kNumNotes + size_t(n)] = std::min(low[size_t(f) * kLowNotes + size_t(n)], spec.rawAt(f, n));
    return e;
}

// Energies per frame and note: the bass register from the long windows (pitch), limited in time by the short window of the
// harmonic part of the note map (a long window rings before an onset and after a stop), the rest the gated harmonic part (as in 4.3).
std::vector<float> mixedEnergies(const Spectrogram& spec, const std::vector<float>& low) {
    std::vector<float> e = spec.harmonic;
    if (low.size() != size_t(spec.frames) * kLowNotes) return e;
    const std::vector<float> gate = e;
    for (int f = 0; f < spec.frames; ++f) {
        const float* g = &gate[size_t(f) * kNumNotes];
        for (int n = 0; n < kLowNotes; ++n)
            e[size_t(f) * kNumNotes + size_t(n)] = std::min(low[size_t(f) * kLowNotes + size_t(n)], g[n]);
    }
    return e;
}

// Window leakage: within a few bins of the 4096 window a pick far below a stronger pick is the sidelobe of that note, not a note.
void removeLeakage(std::vector<float>& picks, const std::vector<float>& energies, int frames) {
    const double binHz = kLeakageBinHz;
    for (int f = 0; f < frames; ++f) {
        float* row = &picks[size_t(f) * kNumNotes];
        const float* e = &energies[size_t(f) * kNumNotes];
        for (int n = 0; n < kNumNotes; ++n) {
            if (row[n] <= 0.0f) continue;
            const double hz = 440.0 * std::pow(2.0, (kMinNote + n - 69) / 12.0);
            if (hz > kLeakageMaxBins * binHz * 4.0) break;  // beyond the register where semitones sit within a few bins
            for (int m = 0; m < kNumNotes; ++m) {
                const double dist = std::fabs(440.0 * std::pow(2.0, (kMinNote + m - 69) / 12.0) - hz) / binHz;
                if (m != n && dist <= kLeakageMaxBins && e[n] < kLeakageRatio * e[m]) { row[n] = 0.0f; break; }
            }
        }
    }
}

void medianInPlace(std::vector<float>& values, int frames) {
    const std::vector<float> src = values;
    const int half = kSmoothFrames / 2;
    for (int n = 0; n < kNumNotes; ++n)
        for (int f = 0; f < frames; ++f) {
            float win[kSmoothFrames];
            for (int k = 0; k < kSmoothFrames; ++k) win[k] = src[size_t(std::clamp(f + k - half, 0, frames - 1)) * kNumNotes + size_t(n)];
            std::sort(win, win + kSmoothFrames);
            values[size_t(f) * kNumNotes + size_t(n)] = win[half];
        }
}

// Strongest level of the octave band that is held for at least kHeldFrames (an eroded maximum curve): leakage and transients
// of a band without any held note do not set the reference.
float heldBandMax(const std::vector<float>& values, int frames, int first, int last) {
    std::vector<float> curve(size_t(frames), 0.0f);
    for (int f = 0; f < frames; ++f)
        for (int n = first; n < last; ++n) curve[size_t(f)] = std::max(curve[size_t(f)], values[size_t(f) * kNumNotes + size_t(n)]);
    float best = 0.0f;
    for (int f = 0; f + kHeldFrames <= frames; ++f) best = std::max(best, *std::min_element(curve.begin() + f, curve.begin() + f + kHeldFrames));
    return best;
}

// Every value relative to the held maximum of its octave band (not below the global floor). Returns per note the factor
// that turns a normalised value back into the share of the global maximum.
std::vector<float> normalisePerBand(std::vector<float>& values, int frames) {
    std::vector<float> toGlobal(kNumNotes, 1.0f);
    const float globalMax = *std::max_element(values.begin(), values.end());
    for (int first = 0; first < kNumNotes; first += 12) {
        const int last = std::min(kNumNotes, first + 12);
        const float ref = std::max(heldBandMax(values, frames, first, last), globalMax * dbToLin(kBandFloorDb));
        for (int f = 0; f < frames; ++f)
            for (int n = first; n < last; ++n) values[size_t(f) * kNumNotes + size_t(n)] /= ref;
        for (int n = first; n < last; ++n) toGlobal[size_t(n)] = ref / globalMax;
    }
    return toGlobal;
}

// The kick sweeps through the bass notes for 0.1-0.3 s right after the beat: a short low note that starts there is the kick.
bool startsOnBeatHit(const std::vector<char>& beatHits, const Run& r) {
    for (int f = std::max(0, r.first - kKickLeadFrames); f <= r.first + kKickLeadFrames && f < int(beatHits.size()); ++f)
        if (beatHits[size_t(f)]) return true;
    return false;
}

// Noise has no spectral peak: a tone stands out of the notes around it, a noise band does not (spectral flatness gate).
bool isNoiseLike(const Spectrogram& spec, const Run& r, int note) {
    if ((r.last - r.first + 1) * spec.hopSec >= kNoiseGateMaxSec) return false;  // a held line is no noise, however loud the drums are
    double own = 0.0, around = 0.0;
    int count = 0;
    for (int f = r.first; f <= r.last; ++f)
        for (int k = kProminenceNear; k <= kProminenceFar; ++k)
            for (int side = -1; side <= 1; side += 2) {
                const int m = note + side * k;
                if (m < 0 || m >= kNumNotes) continue;
                around += double(spec.rawAt(f, m));
                ++count;
            }
    for (int f = r.first; f <= r.last; ++f) own += double(spec.rawAt(f, note));
    if (count == 0) return false;
    const double ownMean = own / double(r.last - r.first + 1), aroundMean = around / double(count);
    return ownMean < kMinProminence * aroundMean;
}

// A short low run that begins within kKickTailLeadSec after a kick on the beat and decays is the tail of that kick (an offbeat bass note rises or stays).
bool isKickTail(const std::vector<char>& beatHits, const std::vector<float>& level, const Run& r, double hop, int note) {
    const int frames = r.last - r.first + 1;
    if (kMinNote + note >= kKickTopNote || frames * hop >= kKickPulseMaxSec || frames < 4) return false;
    const int lead = int(std::ceil(kKickTailLeadSec / hop));
    bool isAfterKick = false;
    for (int f = std::max(0, r.first - lead); f <= r.first; ++f) isAfterKick = isAfterKick || beatHits[size_t(f)];
    if (!isAfterKick) return false;
    double first = 0.0, second = 0.0;
    for (int f = r.first; f <= r.last; ++f) (f - r.first < frames / 2 ? first : second) += double(level[size_t(f)]);
    return second < kKickTailDecay * first;
}

bool isKickBlip(const std::vector<char>& beatHits, const Run& r, double hop, int note) {
    return kMinNote + note < kKickTopNote && (r.last - r.first + 1) * hop < kKickBlipMaxSec && startsOnBeatHit(beatHits, r);
}

// Short and weak segments are almost always leakage or noise: below 150 ms they need a mean level of -16 dB (as in 4.3), below
// 300 ms still -30 dB (relative to the strongest pick).
bool isWeakBlip(const std::vector<float>& v, const Run& r, float toGlobal, double hop) {
    const double length = (r.last - r.first + 1) * hop;
    if (length >= kWeakUntilSec) return false;
    double sum = 0.0;
    for (int f = r.first; f <= r.last; ++f) sum += double(v[size_t(f)]);
    const float db = 20.0f * std::log10(std::max(float(sum / (r.last - r.first + 1)) * toGlobal, 1e-4f));
    return db < (length < kShortNoteSec ? kWeakShortDb : kWeakMediumDb);
}

// A pitch difference within a fifth, or that of the 2nd / 3rd / 4th / 5th partial: m is then leakage or a sub-harmonic of n.
bool isNeighbourOrSubharmonic(int semitonesAbove) {
    const int d = std::abs(semitonesAbove);
    return d <= kGhostSemitones || (semitonesAbove > 0 && (d == 12 || d == 19 || d == 24 || d == 28));
}

// A weak low note beside a much stronger note that sounds at the same time is its leakage or sub-harmonic, not a note.
std::vector<NoteEvent> dropGhosts(const std::vector<NoteEvent>& notes) {
    std::vector<NoteEvent> kept;
    for (const NoteEvent& m : notes) {
        bool isGhost = false;
        const bool isLow = m.pitch < kKickTopNote, isHigh = m.pitch >= kHighGhostNote;  // leakage below, overtones beyond the cancelled series above
        if (isLow || isHigh)
            for (const NoteEvent& n : notes) {
                if (&n == &m || n.strength < m.strength * kGhostRatio || (isLow && !isNeighbourOrSubharmonic(n.pitch - m.pitch))) continue;
                // the window of a strong note rings on after its end: its span counts kGhostRingSec longer on both sides
                const double overlap = std::min(m.endSec, n.endSec + kGhostRingSec) - std::max(m.startSec, n.startSec - kGhostRingSec);
                isGhost = isGhost || overlap >= kGhostOverlap * (m.endSec - m.startSec);
            }
        if (!isGhost) kept.push_back(m);
    }
    return kept;
}

}  // namespace

double minNoteSecForTempo(double bpm) { return bpm >= 40.0 && bpm <= 300.0 ? 15.0 / bpm : kFallbackMinSec; }

std::vector<NoteEvent> sustainGrabNotes(const Spectrogram& spec, const std::vector<float>& low, float amount, const GrabTempo& tempo, TaskControl* control) {
    std::vector<NoteEvent> notes;
    if (spec.frames == 0) return notes;
    const std::vector<float> harmonicSupport = mixedEnergies(spec, low);  // sharp edges: new attacks are looked for here
    std::vector<float> bridgeSupport = rawEnergies(spec, low);
    medianInPlace(bridgeSupport, spec.frames);
    std::vector<float> support = harmonicSupport;
    medianInPlace(support, spec.frames);
    std::vector<float> s = pickFundamentals(support, spec.frames, control, {kSubtractGain, kExplainFactor});
    if (s.empty() || (control && control->cancelled())) return notes;
    removeLeakage(s, support, spec.frames);
    const std::vector<float> sharp = s;  // picks before the smoothing: valleys and attacks of one pitch
    medianInPlace(s, spec.frames);
    const std::vector<float> toGlobal = normalisePerBand(s, spec.frames);

    const BeatGrid grid = findBeatGrid(spec, tempo.bpm, tempo.startPpq, kDrumFilter);
    const std::vector<char> beatHits = beatHitFrames(spec, grid, kDrumFilter);
    const std::vector<char> onBeat = beatFrames(spec, grid);
    const std::vector<char> hits = percussiveFrames(spec, kDrumFilter, kHitBeforeSec, kHitAfterSec);
    const int shortBridge = int(kShortBridgeSec / spec.hopSec);
    const int longBridge = int(std::clamp(eighthNoteSec(tempo.bpm), kMinBridgeSec, kMaxBridgeSec) / spec.hopSec);
    const int heldThroughMax = int(kHeldThroughMaxSec / spec.hopSec);
    const int minFrames = int(std::ceil(minNoteSecForTempo(tempo.bpm) / spec.hopSec));
    const float onRelative = dbToLin(sensitivityToThresholdDb(amount));
    const float globalLimit = dbToLin(sensitivityToThresholdDb(1.0f));  // the band reference never lowers the threshold below the global one at 100 %

    std::vector<float> column(size_t(spec.frames)), supportColumn(size_t(spec.frames)), attackColumn(size_t(spec.frames)), sharpColumn(size_t(spec.frames)), pulseColumn(size_t(spec.frames));
    for (int n = 0; n < kNumNotes; ++n) {
        if (control && n % 8 == 0 && control->cancelled()) return {};
        for (int f = 0; f < spec.frames; ++f) {
            column[size_t(f)] = s[size_t(f) * kNumNotes + size_t(n)];
            pulseColumn[size_t(f)] = support[size_t(f) * kNumNotes + size_t(n)];
            supportColumn[size_t(f)] = bridgeSupport[size_t(f) * kNumNotes + size_t(n)];
            sharpColumn[size_t(f)] = sharp[size_t(f) * kNumNotes + size_t(n)];
            attackColumn[size_t(f)] = harmonicSupport[size_t(f) * kNumNotes + size_t(n)];
        }
        const float on = std::max(onRelative, globalLimit / toGlobal[size_t(n)]), off = on * dbToLin(-kHysteresisDb);
        const std::vector<char>& mask = kMinNote + n < kKickTopNote ? onBeat : beatHits;  // kick range: any beat; above: where a hit was seen
        const std::vector<float>& maskSupport = kMinNote + n < kKickTopNote ? supportColumn : attackColumn;  // raw level in the kick range, harmonic above
        for (const Run& found : bridgeRuns(supportColumn, attackColumn, maskSupport, mask, valleyRuns(sharpColumn, sharpColumn, maskSupport, mask, longBridge, valleyRuns(attackColumn, attackColumn, maskSupport, mask, longBridge, hysteresisRuns(column, on, off))), shortBridge, longBridge, kMinNote + n >= kKickTopNote ? heldThroughMax : 0)) {  // below C3 the raw level is the kick's
            const Run r = trimRun(column, found, kEdgeTrimRatio);
            if (r.last - r.first + 1 < minFrames || isWeakBlip(column, r, toGlobal[size_t(n)], spec.hopSec)) continue;
            if (isKickBlip(beatHits, r, spec.hopSec, n)) continue;  // short bass tone that starts with a kick on the beat
            if (isNoiseLike(spec, r, n)) continue;
            if (isKickTail(beatHits, pulseColumn, r, spec.hopSec, n)) continue;
            const bool isKickRange = kMinNote + n < kKickTopNote && (r.last - r.first + 1) * spec.hopSec < kKickPulseMaxSec;
            if (isBeatPulse(pulseColumn, r.first, r.last, spec.hopSec, grid, isKickRange ? 1 : 2)) continue;  // kick tail, not a held tone
            NoteSegment seg;
            seg.noteIndex = n;
            seg.first = r.first;
            seg.last = r.last;
            seg.note.pitch = kMinNote + n;
            seg.note.startSec = std::max(0.0, r.first * spec.hopSec - spec.hopSec / 2);
            seg.note.endSec = std::min(spec.lengthSec, r.last * spec.hopSec + spec.hopSec / 2);
            annotateSegment(spec, &seg);
            const DrumReason reason = drumReason(spec, seg, kDrumFilter);
            // a decaying note after its own onset is no drum tail unless it is a low note that starts with a kick on the beat
            const bool isTailOk = reason == DrumReason::DrumTail && !(kMinNote + n < kKickTopNote && startsOnBeatHit(beatHits, r));
            if (reason != DrumReason::None && !isTailOk) continue;
            double sum = 0.0;
            for (int f = r.first; f <= r.last; ++f) sum += double(column[size_t(f)]);
            seg.note.strength = float(sum / double(r.last - r.first + 1)) * toGlobal[size_t(n)];  // level for the ghost check, reset by the caller
            notes.push_back(seg.note);
        }
    }
    notes = dropGhosts(notes);
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.startSec != b.startSec ? a.startSec < b.startSec : a.pitch < b.pitch; });
    if (control) control->report(1.0f);
    return notes;
}

}  // namespace ttr
