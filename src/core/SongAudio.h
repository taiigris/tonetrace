// Building and reading the player's audio: 16 bit interleaved from float, the waveform overview, and the mono
// signal for the model.
#pragma once
#include <cstddef>
#include <memory>
#include <vector>

#include "core/Types.h"

namespace ttr {

// `interleaved` holds frames * channels floats (-1..1). Channels beyond two are dropped.
std::shared_ptr<SongAudio> makeSongAudio(const float* interleaved, size_t frames, int channels, double sampleRate);
// Takes over a decoded 16 bit buffer without copying it.
std::shared_ptr<SongAudio> makeSongAudio(std::vector<int16_t>&& interleaved, int channels, double sampleRate);
// Two mono buffers (a recording) as stereo.
std::shared_ptr<SongAudio> makeSongAudio(const float* left, const float* right, size_t frames, double sampleRate);
std::shared_ptr<WavePeaks> computePeaks(const SongAudio& audio);
// All channels averaged, as float.
std::vector<float> monoOf(const SongAudio& audio);
// All channels averaged, resampled to `rate` (the model's input, without a full-rate float copy in between).
std::vector<float> monoAt(const SongAudio& audio, double rate);
// Linear-interpolated stereo sample at fractional frame `pos` (mono audio gives the same value twice). 0 outside.
void sampleAt(const SongAudio& audio, double pos, float* left, float* right);

}  // namespace ttr
