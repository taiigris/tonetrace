// Builds the stored Spectrogram (raw + harmonic note energies, percussive descriptors) from audio.
#pragma once
#include <cstddef>
#include <memory>

#include "core/Types.h"

namespace ttr {

constexpr double kHpssTimeSec = 0.200;   // median window over time (harmonic)
constexpr double kHpssFreqHz = 250.0;    // median window over frequency (percussive)

// Returns nullptr for empty / silent input.
std::shared_ptr<Spectrogram> computeSpectrogram(const float* mono, size_t count, double sampleRate);

}  // namespace ttr
