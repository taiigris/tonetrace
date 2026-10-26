#include "analysis/LineOnsets.h"

#include <algorithm>
#include <cmath>
#include <memory>

#include "analysis/Fft.h"

namespace ttr {

namespace {

constexpr int kWindow = 512, kHop = 128;
constexpr double kMinHz = 80.0, kMaxHz = 6000.0;
constexpr float kCompression = 2000.0f;       // log(1 + c * amplitude)
constexpr int kContextFrames = 40;            // +-0.23 s around a candidate
constexpr float kContextFactor = 2.2f, kMinFlux = 8.0f;
constexpr int kStrongContextFrames = 86;      // +-0.5 s
constexpr float kRelativeFlux = 0.15f;        // a real onset is not far below the strongest one nearby (beating of steady tones is)
constexpr double kOnsetBiasSec = 0.003;       // the log-compressed flux reacts to the first fade-in samples
constexpr int kPeakRadius = 4;                // local maximum within +-23 ms
constexpr double kMinGapSec = 0.040;

}  // namespace

std::vector<float> detectOnsets(const float* samples, long count, double rate) {
    std::vector<float> onsets;
    if (!samples || count < kWindow * 2 || rate <= 0.0) return onsets;
    const int frames = int(count / kHop) + 1;
    auto fft = createFft(9);
    std::vector<float> window(kWindow), frame(kWindow), re(kWindow / 2 + 1), im(kWindow / 2 + 1);
    for (int i = 0; i < kWindow; ++i) window[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / kWindow));
    const int firstBin = std::max(1, int(kMinHz / (rate / kWindow))), lastBin = std::min(kWindow / 2 - 1, int(kMaxHz / (rate / kWindow)));
    const float scale = 4.0f / float(kWindow);

    std::vector<float> previous(size_t(lastBin + 1), 0.0f), flux(size_t(frames), 0.0f);
    for (int f = 0; f < frames; ++f) {
        const long start = long(f) * kHop - kWindow / 2;
        for (int i = 0; i < kWindow; ++i) {
            const long idx = start + i;
            frame[size_t(i)] = idx >= 0 && idx < count ? samples[idx] * window[size_t(i)] : 0.0f;
        }
        fft->forward(frame.data(), re.data(), im.data());
        float sum = 0.0f;
        for (int k = firstBin; k <= lastBin; ++k) {
            const float c = std::log1p(kCompression * scale * std::sqrt(re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)]));
            sum += std::max(0.0f, c - previous[size_t(k)]);
            previous[size_t(k)] = c;
        }
        flux[size_t(f)] = f == 0 ? 0.0f : sum;
    }

    std::vector<float> context;
    double lastTime = -1.0;
    for (int f = 1; f < frames; ++f) {
        const float v = flux[size_t(f)];
        if (v < kMinFlux) continue;
        bool isPeak = true;
        for (int k = std::max(0, f - kPeakRadius); k <= std::min(frames - 1, f + kPeakRadius) && isPeak; ++k)
            if (k != f && (flux[size_t(k)] > v || (flux[size_t(k)] == v && k < f))) isPeak = false;
        if (!isPeak) continue;
        context.assign(flux.begin() + std::max(0, f - kContextFrames), flux.begin() + std::min(frames, f + kContextFrames + 1));
        std::nth_element(context.begin(), context.begin() + ptrdiff_t(context.size() / 2), context.end());
        if (v < kContextFactor * context[context.size() / 2] + kMinFlux) continue;
        const float strongest = *std::max_element(flux.begin() + std::max(0, f - kStrongContextFrames), flux.begin() + std::min(frames, f + kStrongContextFrames + 1));
        if (v < kRelativeFlux * strongest) continue;
        // flux[f] compares frame f with f-1: the change is centred half a hop earlier; refine with the neighbours
        const float a = flux[size_t(f - 1)], c = f + 1 < frames ? flux[size_t(f + 1)] : 0.0f;
        const double centroid = (double(a) * (f - 1.5) + double(v) * (f - 0.5) + double(c) * (f + 0.5)) / (double(a) + double(v) + double(c));
        const double time = centroid * kHop / rate + kOnsetBiasSec;
        if (time - lastTime < kMinGapSec) continue;
        onsets.push_back(float(time));
        lastTime = time;
    }
    return onsets;
}

}  // namespace ttr
