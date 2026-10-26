// Harmonic/percussive separation by median filtering (Fitzgerald-style) on a magnitude spectrogram.
#pragma once
#include <vector>

namespace ttr {

// Dense tonal material gets a soft mask of ~0.8 and would distort the partial ratios the harmonic
// cancellation relies on, so the soft mask is contrast-stretched: <= low -> 0, >= high -> 1.
constexpr float kHpssMaskLow = 0.2f, kHpssMaskHigh = 0.8f;
constexpr float kHpssEpsRel = 1e-3f;  // eps = (kHpssEpsRel * global max)^2

// Mask value of one bin from its time-median (harmonic) and frequency-median (percussive) magnitude.
inline float hpssMask(float harm, float perc, float eps) {
    const float h2 = harm * harm, p2 = perc * perc;
    const float soft = h2 / (h2 + p2 + eps);
    const float m = (soft - kHpssMaskLow) / (kHpssMaskHigh - kHpssMaskLow);
    return m < 0.0f ? 0.0f : (m > 1.0f ? 1.0f : m);
}

int hpssOddWindow(int n);

struct HpssResult {
    std::vector<float> mask;      // frames * bins, soft harmonic mask M_H = H^2 / (H^2 + P^2 + eps)
    // Per frame onset descriptors, both 0..1 and measured as the excess over their local median:
    std::vector<float> percFrac;  // share of the (compressed) spectrum not explained by H, i.e. percussive part
    std::vector<float> flux;      // positive spectral flux
};

// timeWin / freqWin are forced odd. mags: frames * bins (linear magnitude).
HpssResult runHpss(const std::vector<float>& mags, int frames, int bins, int timeWin, int freqWin);

}  // namespace ttr
