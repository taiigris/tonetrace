#include "render/GlowProfile.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {

struct Stop { float at, r, g, b; };

constexpr Stop kWarmStops[] = {{0.00f, 0, 0, 0},       {0.07f, 22, 6, 44},    {0.20f, 76, 14, 120},  {0.32f, 156, 18, 112}, {0.45f, 214, 30, 44},
                               {0.60f, 244, 96, 18},   {0.80f, 255, 206, 48}, {1.00f, 255, 252, 232}};
constexpr Stop kCoolStops[] = {{0.00f, 0, 0, 0}, {0.30f, 26, 24, 74}, {0.70f, 74, 84, 176}, {1.00f, 150, 168, 236}};

constexpr float kShineBase = 1.4f, kShineGrow = 1.9f;   // shine sigma in points: 1.4 (weak) to 3.3 (strong)
constexpr float kHazeFactor = 2.0f, kHazeStart = 0.12f, kHazeFull = 0.45f;
constexpr float kCoreSigmaWeak = 0.8f, kCoolSigmaPt = 0.7f;
constexpr float kSigmaReach = 2.5f;                       // profiles are cut at this many sigmas
constexpr float kCoolCutoff = 3.0f;

template <size_t N> GlowLut makeLut(const Stop (&stops)[N]) {
    GlowLut lut{};
    for (int i = 0; i < kGlowLutSize; ++i) {
        const float x = float(i) / float(kGlowLutSize - 1);
        size_t k = 0;
        while (k + 2 < N && x > stops[k + 1].at) ++k;
        const float t = std::clamp((x - stops[k].at) / (stops[k + 1].at - stops[k].at), 0.0f, 1.0f);
        auto mix = [&](float a, float b) { return uint32_t(std::lround(a + (b - a) * t)); };
        lut[size_t(i)] = (mix(stops[k].r, stops[k + 1].r) << 16) | (mix(stops[k].g, stops[k + 1].g) << 8) | mix(stops[k].b, stops[k + 1].b);
    }
    return lut;
}

float gauss(float d, float sigma) { return std::exp(-d * d / (2.0f * sigma * sigma)); }

struct Sigmas { float core, shine, haze, hazeAmp; };

Sigmas sigmasOf(float brightness, double scale) {
    const float b = std::clamp(brightness, 0.0f, 1.0f), s = float(scale);
    Sigmas out;
    out.core = (kCoreSigmaWeak + (kGlowCoreSigmaPt - kCoreSigmaWeak) * b) * s;
    out.shine = (kShineBase + kShineGrow * b) * s;
    out.haze = kHazeFactor * out.shine;
    out.hazeAmp = kGlowHazeAmp * std::clamp((b - kHazeStart) / (kHazeFull - kHazeStart), 0.0f, 1.0f);
    return out;
}

}  // namespace

const GlowLut& glowWarmLut() { static const GlowLut lut = makeLut(kWarmStops); return lut; }
const GlowLut& glowCoolLut() { static const GlowLut lut = makeLut(kCoolStops); return lut; }

uint32_t glowLookup(const GlowLut& lut, float intensity) {
    const float x = std::isfinite(intensity) ? std::clamp(intensity, 0.0f, 1.0f) : 0.0f;
    return lut[size_t(x * float(kGlowLutSize - 1) + 0.5f)];
}

float glowProfileAt(float d, float brightness, double scale) {
    const Sigmas s = sigmasOf(brightness, scale);
    return kGlowCoreAmp * gauss(d, s.core) + kGlowShineAmp * gauss(d, s.shine) + s.hazeAmp * gauss(d, s.haze);
}

int GlowProfiles::classOf(float brightness) { return std::clamp(int(brightness * kGlowClasses), 0, kGlowClasses - 1); }
int GlowProfiles::phaseOf(float fraction) { return std::clamp(int(fraction * kGlowPhases), 0, kGlowPhases - 1); }

GlowProfiles::GlowProfiles(double scale) {
    for (int c = 0; c < kGlowClasses; ++c) {
        const float b = (float(c) + 0.5f) / kGlowClasses;
        const Sigmas s = sigmasOf(b, scale);
        const int r = int(std::ceil(kSigmaReach * (s.hazeAmp > 0.0f ? s.haze : s.shine)));
        radius_[c] = r;
        maxRadius_ = std::max(maxRadius_, r);
        for (int p = 0; p < kGlowPhases; ++p) {
            std::vector<float>& t = taps_[c * kGlowPhases + p];
            t.resize(size_t(2 * r + 1));
            for (int k = -r; k <= r; ++k) t[size_t(k + r)] = glowProfileAt(float(k) + 0.5f - (float(p) + 0.5f) / kGlowPhases, b, scale);
        }
    }
    const float sigma = kCoolSigmaPt * float(scale);
    coolRadius_ = int(std::ceil(kCoolCutoff * sigma));
    maxRadius_ = std::max(maxRadius_, coolRadius_);
    for (int p = 0; p < kGlowPhases; ++p) {
        cool_[p].resize(size_t(2 * coolRadius_ + 1));
        for (int k = -coolRadius_; k <= coolRadius_; ++k) cool_[p][size_t(k + coolRadius_)] = gauss(float(k) + 0.5f - (float(p) + 0.5f) / kGlowPhases, sigma);
    }
}

}  // namespace ttr
