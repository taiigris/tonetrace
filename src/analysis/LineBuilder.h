// Builds the pitch lines of a whole recording: frames, tracks, onsets, harmonic / drum flags and the tuning estimate.
#pragma once
#include <memory>
#include <vector>

#include "analysis/PitchLines.h"
#include "analysis/TaskControl.h"

namespace ttr {

// `samples` is the decimated mono signal (see decimate()), `rate` its sample rate. With a spectrogram of the same
// recording the drum filter flags percussive lines. Returns null for silence or when `control` cancels.
std::shared_ptr<PitchLines> computePitchLines(const float* samples, long count, double rate, const Spectrogram* spectrogram, TaskControl* control = nullptr);

// Second half of the analysis: `lines` holds the per-frame points (tracks assigned); fills tracks, flags, onsets, tuning.
void finalizeLines(PitchLines* lines, std::vector<float> onsets, const Spectrogram* spectrogram);

}  // namespace ttr
