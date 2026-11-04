// The analysis that turns mono audio into a Recording (spectrogram, waveform, stored clip, pitch lines, key chroma).
// Shared by a finished recording (worker thread) and an imported song (4.7).
#pragma once
#include <cstddef>

#include "analysis/TaskControl.h"
#include "core/Snapshot.h"

namespace ttr {

constexpr double kMaxStoredAudioSeconds = 60.0;  // recordings: longer ones keep no audio clip (state size)

// The audio decimated to 16 bit for the preview and the ML engines; null when longer than `maxStoredSeconds`.
std::shared_ptr<const AudioClip> makeClip(const float* samples, size_t count, double sampleRate, double maxStoredSeconds);

// Everything but tempo, start and name. No spectrogram (and nothing else) when the signal is empty or silent.
// `control` may cancel the pitch lines (the most expensive part); the caller checks control->cancelled() afterwards.
Recording analyseAudio(const float* samples, size_t count, double sampleRate, double maxStoredSeconds, TaskControl* control = nullptr);

}  // namespace ttr
