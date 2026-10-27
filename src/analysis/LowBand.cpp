#include "analysis/LowBand.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "analysis/Fft.h"

namespace ttr {

namespace {

constexpr int kPadFactor = 2;           // zero padding: bin spacing of 1 / (pad * window)
constexpr int kReferenceWindow = 4096;  // the units of Spectrogram::raw

struct WindowBank {
    int window = 0, fftSize = 0;
    std::unique_ptr<RealFft> fft;
    std::vector<float> hann, frame, re, im, mag;
    explicit WindowBank(int w) : window(w), fftSize(w * kPadFactor) {
        int log2n = 0;
        while ((1 << log2n) < fftSize) ++log2n;
        fft = createFft(log2n);
        hann.resize(size_t(w));
        for (int i = 0; i < w; ++i) hann[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / w));
        frame.assign(size_t(fftSize), 0.0f);
        re.resize(size_t(fftSize / 2 + 1));
        im.resize(size_t(fftSize / 2 + 1));
        mag.resize(size_t(fftSize / 2 + 1));
    }
    void analyse(const float* x, long count, long centre) {
        std::fill(frame.begin(), frame.end(), 0.0f);
        const long start = centre - window / 2;
        for (int i = 0; i < window; ++i) {
            const long idx = start + i;
            if (idx >= 0 && idx < count) frame[size_t(i)] = x[idx] * hann[size_t(i)];
        }
        fft->forward(frame.data(), re.data(), im.data());
        for (size_t k = 0; k < mag.size(); ++k) mag[k] = std::sqrt(re[k] * re[k] + im[k] * im[k]);
    }
    // Strongest magnitude within +-50 cent of `hz`, normalised to the reference window.
    float energyAt(double hz, double rate) const {
        const double binHz = rate / fftSize;
        const int lo = std::max(1, int(std::ceil(hz * std::pow(2.0, -1.0 / 24.0) / binHz)));
        const int hi = std::min(int(mag.size()) - 2, int(std::floor(hz * std::pow(2.0, 1.0 / 24.0) / binHz)));
        float best = 0.0f;
        if (hi < lo) {
            const int k = std::min(int(hz / binHz), int(mag.size()) - 2);
            const float frac = float(hz / binHz - k);
            best = mag[size_t(k)] * (1.0f - frac) + mag[size_t(k) + 1] * frac;
        }
        for (int k = lo; k <= hi; ++k) best = std::max(best, mag[size_t(k)]);
        return best * float(kReferenceWindow) / float(window);
    }
};

}  // namespace

std::vector<float> computeLowEnergies(const float* samples, long count, double rate, int frames, int hop) {
    std::vector<float> out(size_t(frames) * kLowNotes, 0.0f);
    if (frames <= 0 || !samples) return out;
    WindowBank longBank(kLowLongWindow), shortBank(kLowShortWindow);
    std::vector<double> hz(kLowNotes);
    for (int n = 0; n < kLowNotes; ++n) hz[size_t(n)] = 440.0 * std::pow(2.0, (kLowFirstNote + n - 69) / 12.0);
    for (int f = 0; f < frames; f += 2) {
        longBank.analyse(samples, count, long(f) * hop);
        shortBank.analyse(samples, count, long(f) * hop);
        for (int n = 0; n < kLowNotes; ++n) {
            const bool isLong = kLowFirstNote + n < kLowLongTopNote;
            out[size_t(f) * kLowNotes + size_t(n)] = (isLong ? longBank : shortBank).energyAt(hz[size_t(n)], rate);
        }
    }
    for (int f = 1; f < frames; f += 2)
        for (int n = 0; n < kLowNotes; ++n) {
            const float before = out[size_t(f - 1) * kLowNotes + size_t(n)];
            const float after = f + 1 < frames ? out[size_t(f + 1) * kLowNotes + size_t(n)] : before;
            out[size_t(f) * kLowNotes + size_t(n)] = 0.5f * (before + after);
        }
    return out;
}

}  // namespace ttr
