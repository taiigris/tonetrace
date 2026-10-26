#include "analysis/SpectralFrames.h"

#include <algorithm>
#include <cmath>

#include "analysis/Fft.h"

namespace ttr {

Decimator::Decimator(double sampleRate) {
    factor_ = std::max(1, int(std::lround(sampleRate / kTargetRate)));
    outputRate_ = sampleRate / factor_;
    if (factor_ == 1) return;
    half_ = 8 * factor_;
    const int count = 2 * half_ + 1;
    const double cutoff = 0.45 / factor_;
    taps_.resize(size_t(count));
    double sum = 0.0;
    for (int k = 0; k < count; ++k) {
        const double x = k - half_;
        const double sinc = x == 0.0 ? 2.0 * cutoff : std::sin(2.0 * M_PI * cutoff * x) / (M_PI * x);
        const double win = 0.5 + 0.5 * std::cos(M_PI * x / (half_ + 1));
        taps_[size_t(k)] = float(sinc * win);
        sum += taps_[size_t(k)];
    }
    for (float& v : taps_) v = float(v / sum);
}

float Decimator::at(const float* in, size_t count, size_t i) const {
    if (factor_ == 1) return in[i];
    const long center = long(i) * factor_;
    double acc = 0.0;
    for (int k = 0; k < int(taps_.size()); ++k) {
        const long idx = center + k - half_;
        if (idx >= 0 && idx < long(count)) acc += double(taps_[size_t(k)]) * in[idx];
    }
    return float(acc);
}

DecimatedSignal decimate(const float* in, size_t count, double sampleRate) {
    const Decimator decimator(sampleRate);
    DecimatedSignal out;
    out.sampleRate = decimator.outputRate();
    out.samples.resize(count / size_t(decimator.factor()));
    for (size_t i = 0; i < out.samples.size(); ++i) out.samples[i] = decimator.at(in, count, i);
    return out;
}

namespace {

float bandEnergy(int lowBin, int highBin, double centreBin, const float* mag, int numBins) {
    if (highBin < lowBin) {
        const int k0 = std::min(int(centreBin), numBins - 2);
        const float frac = float(centreBin - k0);
        return mag[k0] * (1.0f - frac) + mag[k0 + 1] * frac;
    }
    float best = 0.0f;
    for (int k = lowBin; k <= highBin && k < numBins; ++k) best = std::max(best, mag[k]);
    return best;
}

}  // namespace

NoteFrameBuilder::NoteFrameBuilder(double decimatedRate) {
    const double binHz = decimatedRate / kFftSize;
    bands_.resize(kNumNotes);
    for (int i = 0; i < kNumNotes; ++i) {
        const double f = 440.0 * std::pow(2.0, (kMinNote + i - 69) / 12.0);
        const double lo = f * std::pow(2.0, -1.0 / 24.0), hi = f * std::pow(2.0, 1.0 / 24.0);
        bands_[size_t(i)] = {int(std::ceil(lo / binHz)), int(std::floor(hi / binHz)), f / binHz};
    }
    window_.resize(kWindowSize);  // periodic Hann
    for (int i = 0; i < kWindowSize; ++i) window_[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / kWindowSize));
    fft_ = createFft(13);
    const int numBins = kFftSize / 2;
    frame_.resize(kFftSize);
    re_.resize(size_t(numBins) + 1);
    im_.resize(size_t(numBins) + 1);
    mag_.resize(size_t(numBins) + 1);
}

void NoteFrameBuilder::compute(const float* samples, long count, int f, float* out) {
    const int numBins = kFftSize / 2;
    std::fill(frame_.begin(), frame_.end(), 0.0f);
    const long start = long(f) * kHopSize - kWindowSize / 2;
    for (int i = 0; i < kWindowSize; ++i) {
        const long idx = start + i;
        if (idx >= 0 && idx < count) frame_[size_t(i)] = samples[idx] * window_[size_t(i)];
    }
    fft_->forward(frame_.data(), re_.data(), im_.data());
    for (int k = 0; k <= numBins; ++k) mag_[size_t(k)] = std::sqrt(re_[size_t(k)] * re_[size_t(k)] + im_[size_t(k)] * im_[size_t(k)]);
    for (int n = 0; n < kNumNotes; ++n) out[n] = bandEnergy(bands_[size_t(n)].lowBin, bands_[size_t(n)].highBin, bands_[size_t(n)].centreBin, mag_.data(), numBins);
}

std::vector<float> computeNoteEnergies(const DecimatedSignal& sig, int* outFrames) {
    const int count = int(sig.samples.size());
    const int frames = count / kHopSize + 1;
    *outFrames = frames;
    std::vector<float> energies(size_t(frames) * kNumNotes, 0.0f);
    NoteFrameBuilder builder(sig.sampleRate);
    for (int f = 0; f < frames; ++f) builder.compute(sig.samples.data(), count, f, &energies[size_t(f) * kNumNotes]);
    return energies;
}

}  // namespace ttr

namespace ttr {

TransientFrameBuilder::TransientFrameBuilder(double decimatedRate) {
    const int half = kTransientWindow / 2;
    binHz_ = decimatedRate / kTransientWindow;
    bins_ = std::min(half, int(kTransientMaxHz / binHz_));
    window_.resize(kTransientWindow);
    for (int i = 0; i < kTransientWindow; ++i) window_[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / kTransientWindow));
    fft_ = createFft(10);
    frame_.resize(kTransientWindow);
    re_.resize(size_t(half) + 1);
    im_.resize(size_t(half) + 1);
}

void TransientFrameBuilder::compute(const float* samples, long count, int f, float* out) {
    const long start = long(f) * kHopSize - kTransientWindow / 2;
    for (int i = 0; i < kTransientWindow; ++i) {
        const long idx = start + i;
        frame_[size_t(i)] = idx >= 0 && idx < count ? samples[idx] * window_[size_t(i)] : 0.0f;
    }
    fft_->forward(frame_.data(), re_.data(), im_.data());
    for (int b = 0; b < bins_; ++b) out[b] = std::sqrt(re_[size_t(b)] * re_[size_t(b)] + im_[size_t(b)] * im_[size_t(b)]);
}

PooledSpectrum computeTransientSpectrum(const DecimatedSignal& sig, int frames) {
    TransientFrameBuilder builder(sig.sampleRate);
    PooledSpectrum out;
    out.binHz = builder.binHz();
    out.bins = builder.bins();
    out.mags.assign(size_t(frames) * size_t(out.bins), 0.0f);
    const long count = long(sig.samples.size());
    for (int f = 0; f < frames; ++f) builder.compute(sig.samples.data(), count, f, &out.mags[size_t(f) * size_t(out.bins)]);
    return out;
}

}  // namespace ttr
