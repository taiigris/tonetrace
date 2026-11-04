#include "core/RecordingBuilder.h"

#include <algorithm>
#include <cmath>

#include "analysis/LineBuilder.h"
#include "analysis/SpectralFrames.h"
#include "analysis/Spectrogram.h"
#include "analysis/WavePeaks.h"

namespace ttr {

namespace {
constexpr float kInt16Scale = 32767.0f;
}  // namespace

std::shared_ptr<const AudioClip> makeClip(const float* samples, size_t count, double sampleRate, double maxStoredSeconds) {
    if (double(count) / sampleRate > maxStoredSeconds) return nullptr;
    const DecimatedSignal reduced = decimate(samples, count, sampleRate);
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = reduced.sampleRate;
    clip->samples.reserve(reduced.samples.size());
    for (float v : reduced.samples) clip->samples.push_back(int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * kInt16Scale)));
    return clip;
}

Recording analyseAudio(const float* samples, size_t count, double sampleRate, double maxStoredSeconds, TaskControl* control) {
    Recording recording;
    recording.lengthSec = double(count) / sampleRate;
    recording.spectrogram = computeSpectrogram(samples, count, sampleRate);
    PeakBuilder peaks;
    peaks.prepare(sampleRate, recording.lengthSec);
    peaks.advance(samples, count);
    recording.peaks = peaks.finish();
    if (!recording.spectrogram) return recording;
    recording.audio = makeClip(samples, count, sampleRate, maxStoredSeconds);
    const DecimatedSignal reduced = decimate(samples, count, sampleRate);
    recording.lines = computePitchLines(reduced.samples.data(), long(reduced.samples.size()), reduced.sampleRate, recording.spectrogram.get(), control);
    recording.chroma = spectrogramChroma(*recording.spectrogram);
    return recording;
}

}  // namespace ttr
