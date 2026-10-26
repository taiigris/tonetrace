#include "analysis/Hpss.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {

constexpr int kBaselineFrames = 31;        // ~0.7 s: baseline for onset descriptors
constexpr float kFluxBaselineGain = 2.0f;  // flux must exceed this multiple of the local median
constexpr float kFluxFloorRel = 0.02f;     // keeps near-silent frames from producing onsets

int makeOdd(int n) { return std::max(3, n | 1); }

float medianOf(float* v, int n) {
    std::nth_element(v, v + n / 2, v + n);
    return v[n / 2];
}

// Median over a centred window with clamped edges.
void slidingMedian(const float* in, int count, int stride, int win, float* out, int outStride) {
    std::vector<float> tmp(static_cast<size_t>(win));
    const int half = win / 2;
    for (int i = 0; i < count; ++i) {
        for (int k = 0; k < win; ++k) {
            const int idx = std::min(count - 1, std::max(0, i + k - half));
            tmp[size_t(k)] = in[size_t(idx) * size_t(stride)];
        }
        out[size_t(i) * size_t(outStride)] = medianOf(tmp.data(), win);
    }
}

// Median over the part of a centred window that lies inside the signal (no edge replication).
std::vector<float> edgeAwareMedian(const std::vector<float>& in, int win) {
    const int count = int(in.size()), half = win / 2;
    std::vector<float> out(in.size()), tmp;
    for (int i = 0; i < count; ++i) {
        tmp.assign(in.begin() + std::max(0, i - half), in.begin() + std::min(count, i + half + 1));
        out[size_t(i)] = medianOf(tmp.data(), int(tmp.size()));
    }
    return out;
}

}  // namespace

int hpssOddWindow(int n) { return makeOdd(n); }

HpssResult runHpss(const std::vector<float>& mags, int frames, int bins, int timeWin, int freqWin) {
    HpssResult res;
    res.mask.assign(size_t(frames) * size_t(bins), 1.0f);
    res.percFrac.assign(size_t(frames), 0.0f);
    res.flux.assign(size_t(frames), 0.0f);
    if (frames == 0 || bins == 0) return res;
    timeWin = makeOdd(timeWin);
    freqWin = makeOdd(freqWin);

    std::vector<float> harm(mags.size()), perc(mags.size());
    for (int b = 0; b < bins; ++b)  // H: median over time per bin
        slidingMedian(&mags[size_t(b)], frames, bins, timeWin, &harm[size_t(b)], bins);
    for (int f = 0; f < frames; ++f)  // P: median over frequency per frame
        slidingMedian(&mags[size_t(f) * size_t(bins)], bins, 1, freqWin, &perc[size_t(f) * size_t(bins)], 1);

    const float globalMax = *std::max_element(mags.begin(), mags.end());
    const float eps = (kHpssEpsRel * globalMax) * (kHpssEpsRel * globalMax);
    for (size_t i = 0; i < mags.size(); ++i) {
        res.mask[i] = hpssMask(harm[i], perc[i], eps);
    }

    // Frame level descriptors on sqrt-compressed magnitudes so quiet hats count next to loud tones. The
    // percussive share is the part of the spectrum the stationary (time-median) estimate cannot explain.
    std::vector<float> prev(size_t(bins), 0.0f), up(static_cast<size_t>(frames)), loud(static_cast<size_t>(frames)), frac(static_cast<size_t>(frames));
    for (int f = 0; f < frames; ++f) {
        double sumMag = 0.0, rise = 0.0, unexplained = 0.0;
        for (int b = 0; b < bins; ++b) {
            const size_t i = size_t(f) * size_t(bins) + size_t(b);
            const float c = std::sqrt(mags[i]);
            sumMag += c;
            unexplained += std::max(0.0f, c - std::sqrt(harm[i]));
            rise += std::max(0.0f, c - prev[size_t(b)]);
            prev[size_t(b)] = c;
        }
        frac[size_t(f)] = float(unexplained / std::max(sumMag, 1e-12));
        up[size_t(f)] = float(rise);  // frame 0 compares against silence
        loud[size_t(f)] = float(sumMag);
    }

    // Dense tonal material (saw chords) has a high percussive share and some flux all the time, so both
    // descriptors are measured as the excess over their local median.
    const std::vector<float> upBase = edgeAwareMedian(up, kBaselineFrames), fracBase = edgeAwareMedian(frac, kBaselineFrames);
    for (int f = 0; f < frames; ++f) {
        const size_t i = size_t(f);
        res.percFrac[i] = std::max(0.0f, (frac[i] - fracBase[i]) / std::max(1.0f - fracBase[i], 1e-3f));
        res.flux[i] = std::max(0.0f, up[i] - kFluxBaselineGain * upBase[i]) / (up[i] + kFluxFloorRel * loud[i] + 1e-12f);
    }
    return res;
}

}  // namespace ttr
