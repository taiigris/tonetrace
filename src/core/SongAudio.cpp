#include "core/SongAudio.h"

#include "analysis/ml/Resampler.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {

constexpr float kToInt16 = 32767.0f;
constexpr float kFromInt16 = 1.0f / 32768.0f;

int16_t toInt16(float v) { return int16_t(std::lround(std::clamp(std::isfinite(v) ? v : 0.0f, -1.0f, 1.0f) * kToInt16)); }

}  // namespace

std::shared_ptr<SongAudio> makeSongAudio(const float* interleaved, size_t frames, int channels, double sampleRate) {
    auto audio = std::make_shared<SongAudio>();
    audio->sampleRate = sampleRate;
    audio->channels = std::clamp(channels, 1, 2);
    audio->samples.resize(frames * size_t(audio->channels));
    for (size_t f = 0; f < frames; ++f)
        for (int c = 0; c < audio->channels; ++c) audio->samples[f * size_t(audio->channels) + size_t(c)] = toInt16(interleaved[f * size_t(channels) + size_t(c)]);
    return audio;
}

std::shared_ptr<SongAudio> makeSongAudio(std::vector<int16_t>&& interleaved, int channels, double sampleRate) {
    auto audio = std::make_shared<SongAudio>();
    audio->sampleRate = sampleRate;
    audio->channels = std::clamp(channels, 1, 2);
    audio->samples = std::move(interleaved);
    audio->samples.resize(audio->samples.size() - audio->samples.size() % size_t(audio->channels));
    return audio;
}

std::shared_ptr<SongAudio> makeSongAudio(const float* left, const float* right, size_t frames, double sampleRate) {
    auto audio = std::make_shared<SongAudio>();
    audio->sampleRate = sampleRate;
    audio->channels = 2;
    audio->samples.resize(frames * 2);
    for (size_t f = 0; f < frames; ++f) {
        audio->samples[2 * f] = toInt16(left[f]);
        audio->samples[2 * f + 1] = toInt16(right[f]);
    }
    return audio;
}

std::shared_ptr<WavePeaks> computePeaks(const SongAudio& audio) {
    auto peaks = std::make_shared<WavePeaks>();
    const size_t frames = audio.frames();
    if (frames == 0 || !(audio.sampleRate > 0.0)) return peaks;
    const double perPeak = audio.sampleRate / kPeaksPerSecond;
    const size_t count = size_t(std::ceil(double(frames) / perPeak));
    peaks->minMax.resize(count * 2);
    const int ch = audio.channels;
    for (size_t p = 0; p < count; ++p) {
        const size_t from = size_t(double(p) * perPeak), to = std::min(frames, size_t(double(p + 1) * perPeak));
        float lo = 0.0f, hi = 0.0f;
        for (size_t f = from; f < to; ++f) {
            float sum = 0.0f;
            for (int c = 0; c < ch; ++c) sum += float(audio.samples[f * size_t(ch) + size_t(c)]);
            const float v = sum / float(ch) * kFromInt16;
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        peaks->minMax[2 * p] = lo;
        peaks->minMax[2 * p + 1] = hi;
    }
    return peaks;
}

std::vector<float> monoOf(const SongAudio& audio) {
    const size_t frames = audio.frames();
    const int ch = audio.channels;
    std::vector<float> mono(frames);
    for (size_t f = 0; f < frames; ++f) {
        float sum = 0.0f;
        for (int c = 0; c < ch; ++c) sum += float(audio.samples[f * size_t(ch) + size_t(c)]);
        mono[f] = sum / float(ch) * kFromInt16;
    }
    return mono;
}

std::vector<float> monoAt(const SongAudio& audio, double rate) {
    const size_t frames = audio.frames();
    const int ch = audio.channels;
    std::vector<int16_t> mono(frames);
    for (size_t f = 0; f < frames; ++f) {
        int sum = 0;
        for (int c = 0; c < ch; ++c) sum += audio.samples[f * size_t(ch) + size_t(c)];
        mono[f] = int16_t(sum / ch);
    }
    return resampleInt16(mono.data(), mono.size(), audio.sampleRate, rate);
}

void sampleAt(const SongAudio& audio, double pos, float* left, float* right) {
    *left = *right = 0.0f;
    const size_t frames = audio.frames();
    if (pos < 0.0 || frames == 0) return;
    const size_t k = size_t(pos);
    if (k >= frames) return;
    const size_t k1 = std::min(k + 1, frames - 1);
    const float frac = float(pos - double(k));
    const int ch = audio.channels;
    auto at = [&](size_t f, int c) { return float(audio.samples[f * size_t(ch) + size_t(c)]) * kFromInt16; };
    *left = at(k, 0) + (at(k1, 0) - at(k, 0)) * frac;
    *right = ch > 1 ? at(k, 1) + (at(k1, 1) - at(k, 1)) * frac : *left;
}

}  // namespace ttr
