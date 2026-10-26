// Note onsets: positive spectral flux of a short-window STFT (6 ms hop), so a note start is located to a few milliseconds.
#pragma once
#include <vector>

namespace ttr {

// Onset times in seconds (ascending) of a mono signal.
std::vector<float> detectOnsets(const float* samples, long count, double rate);

}  // namespace ttr
