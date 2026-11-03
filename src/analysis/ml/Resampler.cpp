#include "analysis/ml/Resampler.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {

constexpr int kHalfTaps = 16;          // zero crossings of the sinc on each side (at the lower of both rates)
constexpr double kCutoffShare = 0.95;  // pass band up to 95 % of the lower Nyquist frequency
constexpr float kInt16Scale = 1.0f / 32767.0f;

double sinc(double x) { return std::abs(x) < 1e-9 ? 1.0 : std::sin(M_PI * x) / (M_PI * x); }

// Blackman window over [-1, 1].
double blackman(double x) {
    if (std::abs(x) >= 1.0) return 0.0;
    const double t = 0.5 * (x + 1.0);
    return 0.42 - 0.5 * std::cos(2.0 * M_PI * t) + 0.08 * std::cos(4.0 * M_PI * t);
}

}  // namespace

std::vector<float> resampleMono(const float* samples, size_t count, double fromRate, double toRate) {
    if (!samples || count == 0 || fromRate <= 0.0 || toRate <= 0.0) return {};
    if (std::abs(fromRate - toRate) < 1e-6) return std::vector<float>(samples, samples + count);
    const double ratio = fromRate / toRate;                          // input samples per output sample
    const double cutoff = kCutoffShare * std::min(1.0, 1.0 / ratio);  // relative to the input Nyquist rate
    const double halfWidth = kHalfTaps / cutoff;                     // input samples on each side
    const size_t outCount = size_t(std::floor(double(count) / ratio));
    std::vector<float> out(outCount);
    for (size_t n = 0; n < outCount; ++n) {
        const double t = double(n) * ratio;
        const long first = std::max(0L, long(std::ceil(t - halfWidth)));
        const long last = std::min(long(count) - 1, long(std::floor(t + halfWidth)));
        double sum = 0.0;
        for (long k = first; k <= last; ++k) {
            const double d = t - double(k);
            sum += double(samples[k]) * cutoff * sinc(cutoff * d) * blackman(d / halfWidth);
        }
        out[n] = float(sum);
    }
    return out;
}

std::vector<float> resampleInt16(const int16_t* samples, size_t count, double fromRate, double toRate) {
    std::vector<float> f(count);
    for (size_t i = 0; i < count; ++i) f[i] = float(samples[i]) * kInt16Scale;
    return resampleMono(f.data(), f.size(), fromRate, toRate);
}

}  // namespace ttr
