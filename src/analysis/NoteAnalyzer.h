// Polyphonic note detection: STFT -> per-note energy -> iterative harmonic cancellation -> segmentation.
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "analysis/TaskControl.h"
#include "core/Types.h"

namespace ttr {

constexpr double kMinNoteSec = 0.060;  // shortest segment extractSegments keeps by default

// Threshold (dB relative to the loudest detected partial) for a sensitivity in 0..1.
float sensitivityToThresholdDb(float sensitivity);

// A detected run of frames for one note (frame indices are inclusive).
struct NoteSegment {
    NoteEvent note;
    int noteIndex = 0;  // pitch - kMinNote
    int first = 0, last = 0;
};

// Salience from a spectrogram: with hideDrums from the gated harmonic spectrum, else from the raw one.
// With `control` the computation reports its progress and returns null when cancelled.
std::shared_ptr<Salience> computeSalience(const Spectrogram& spectrogram, const FilterSettings& filter, TaskControl* control = nullptr);

// Greedy fundamentals per frame with harmonic cancellation (frames * kNumNotes amplitudes, residual after the lower notes' partials
// are subtracted); no normalisation. Null when silent or cancelled.
struct PickOptions {
    float subtractGain = 0.85f;   // share of the predicted overtone (1/h of the fundamental) that is subtracted
    float explainFactor = 0.0f;   // an overtone bin at most this many times the prediction is taken as fully explained
};
std::vector<float> pickFundamentals(const std::vector<float>& energies, int frames, TaskControl* control = nullptr, const PickOptions& options = {});

// Notes from the strongest lines of the salience (Quick Grab). With filter.hideDrums the segment filter
// (tonality / sustain / glide) drops drum hits.
std::vector<NoteEvent> extractNotes(const Salience& salience, float sensitivity, const Spectrogram& spectrogram,
                                    const FilterSettings& filter, double minNoteSec = kMinNoteSec);
std::vector<NoteSegment> extractSegments(const Salience& salience, float sensitivity, double minNoteSec = kMinNoteSec);

}  // namespace ttr
