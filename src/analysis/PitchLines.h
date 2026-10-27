// Pitch lines ("peak map"): per frame the spectral peaks at their exact, interpolated pitch, linked over time into tracks.
// This is what the note map draws (thin lines instead of semitone bars) and what click-to-grab, the onset snap and the
// magnet work on. Pitches are fractional MIDI numbers relative to A4 = 440 Hz; the tuning offset is applied by the callers.
#pragma once
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Types.h"

namespace ttr {

constexpr int kLineSlots = 24;            // peaks kept per frame: up to 16 fundamentals, then overtones
constexpr uint8_t kLineHarmonic = 1;      // overtone of a stronger, lower line
constexpr uint8_t kLineDrum = 2;          // part of a drum hit (Hide drums)
constexpr uint8_t kLineHidden = 4;        // too short to be a line (noise)
constexpr double kTrackToleranceSemis = 0.40;  // +-40 cent between two frames of one line
constexpr int kTrackMaxGapFrames = 2;
constexpr int kMinTrackPoints = 3;
constexpr float kHarmonicDim = 0.2f;      // brightness factor of overtones with "Fundamentals"
constexpr double kOnsetSnapSec = 0.080;   // how far the onset snap reaches
constexpr double kMinLineNoteSec = 0.03;  // shortest note a click creates

// Full spectrum on a fine pitch grid (the "Spectrum" view): kFineBinsPerNote bins per semitone, one byte per bin and frame.
// The byte is the level in dB below full scale: 255 = 0 dBFS, 0 = -kFineFloorDb (a full-scale sine has the amplitude 1.0).
constexpr int kFineBinsPerNote = 8;
constexpr int kFineBins = kNumNotes * kFineBinsPerNote;
constexpr float kFineFloorDb = 100.0f;
inline float fineCodeToAmplitude(uint8_t code) { return code == 0 ? 0.0f : std::pow(10.0f, kFineFloorDb * (float(code) / 255.0f - 1.0f) / 20.0f); }

struct LinePoint {
    float midi = 0.0f;   // fractional MIDI note, A4 = 69.0 at 440 Hz
    float level = 0.0f;  // amplitude of the partial (a full-scale sine is 1.0)
    int32_t track = -1;
    uint8_t flags = 0;   // kLine*
    uint8_t age = 0;     // points of this track so far (saturating); live view hides the first ones
};

struct LineTrack {
    int firstFrame = 0, lastFrame = 0, points = 0;
    float meanMidi = 0.0f, peakLevel = 0.0f;
    double startSec = 0.0, endSec = 0.0;  // note edges: half-level crossings, snapped to a detected onset
    uint8_t flags = 0;
};

struct LineHit {
    int track = -1;
    int frame = 0;
    float midi = 0.0f, level = 0.0f;
};

struct PitchLines {
    double hopSec = 0.0;
    int frames = 0;
    float peak = 0.0f;                     // strongest visible level (display reference)
    std::vector<LinePoint> points;         // frames * kLineSlots, fundamentals first
    std::vector<uint8_t> counts;           // per frame
    std::vector<uint8_t> fine;             // frames * kFineBins: the whole spectrum (see kFineBins); empty when not computed
    std::vector<LineTrack> tracks;         // indexed by LinePoint::track
    std::vector<float> onsets;             // seconds, ascending
    std::vector<float> edges;              // seconds, ascending: onsets and track starts / ends (snap targets)
    float tuningCents = 0.0f;              // deviation of the sample from A = 440 Hz, -50..+50
    float tuningConfidence = 0.0f;         // 0..1; below kTuningMinConfidence the grid is not shifted
    bool hasTuning() const;

    const LinePoint* frame(int f) const { return &points[size_t(f) * kLineSlots]; }

    // Visible line point closest to (sec, midi) within `tolSemis`, looking at the frame nearest to `sec`.
    LineHit hitAt(double sec, double midi, double tolSemis) const;
    // The note a click on `track` creates: the segment's edges and the row of its mean pitch (tuning applied).
    NoteEvent noteForTrack(int track, double tuningCents) const;
    // Nearest edge within `tolSec` of `sec`, else `sec` unchanged.
    double snapToEdge(double sec, double tolSec = kOnsetSnapSec) const;
    // The points of one line in time order (for outlines).
    std::vector<LinePoint> trackPoints(int track, std::vector<int>* frames = nullptr) const;
    // Row (within +-1 of `pitch`) of the line with the most energy between t0 and t1; `pitch` when there is none.
    int magnetPitch(double t0, double t1, int pitch, double tuningCents) const;
};

constexpr float kTuningMinConfidence = 0.35f;
inline bool PitchLines::hasTuning() const { return tuningConfidence >= kTuningMinConfidence; }

// Display brightness 0..1 of a line point: level relative to `reference`, shifted by gain, scaled by range (like the old heat map).
// With `fundamentals` overtones are dimmed to kHarmonicDim.
float lineBrightness(const LinePoint& point, float reference, float gain, float range, bool fundamentals);

// The row a line belongs to once the grid is shifted by the tuning offset.
int rowOfMidi(double midi, double tuningCents);
// Deviation in cent of `midi` from the centre of its row (tuning applied), -50..+50.
double centsFromRow(double midi, double tuningCents);

}  // namespace ttr
