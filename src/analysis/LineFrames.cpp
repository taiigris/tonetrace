#include "analysis/LineFrames.h"

#include <algorithm>
#include <cmath>

#include "analysis/SpectralFrames.h"

namespace ttr {

namespace {

struct BandSpec { int log2n; double loHz, hiHz; };
// Window 16384 resolves E2/F2 (4.9 Hz apart), 8192 resolves a semitone from ~130 Hz on, 4096 from ~250 Hz, 2048 from ~1 kHz.
constexpr BandSpec kBands[] = {{14, 0.0, 125.0}, {13, 125.0, 250.0}, {12, 250.0, 1000.0}, {11, 1000.0, 1.0e9}};
constexpr float kAbsoluteFloor = 3.0e-4f;     // about -70 dBFS
constexpr float kMedianFactor = 8.0f;         // a peak must stand this far above the frame's median magnitude
constexpr double kMinBinDistance = 2.0;       // closer peaks are one partial
constexpr double kSidelobeBins = 12.0;
constexpr double kSidelobeBase = 0.08, kSidelobeRefBins = 3.0, kSidelobeSlope = 2.5;
constexpr float kHarmonicLevelRatio = 0.9f;   // an overtone is weaker than its fundamental
constexpr float kHighHarmonicLevelRatio = 0.5f, kVeryHighHarmonicLevelRatio = 0.25f;  // partials 7+ / 17+ are clearly weaker than the fundamental
constexpr double kVeryHighHarmonicToleranceSemis = 0.20;  // the partial windows overlap up there: be strict
constexpr int kMaxHarmonic = 32, kHighHarmonic = 7, kVeryHighHarmonic = 16;
constexpr double kHarmonicToleranceSemis = 0.30;
constexpr int kMaxCandidates = 400, kRankedCandidates = 96;
constexpr int kMaxFundamentals = 16;  // the rest of the slots goes to overtones
constexpr float kTiny = 1.0e-12f;

double harmonicSemis(int h) { return 12.0 * std::log2(double(h)); }

}  // namespace

int LineFrameAnalyzer::lookahead() { return (1 << kBands[0].log2n) / 2; }

LineFrameAnalyzer::LineFrameAnalyzer(double rate) {
    for (const BandSpec& spec : kBands) {
        Band b;
        b.n = 1 << spec.log2n;
        b.binHz = rate / b.n;
        b.loHz = spec.loHz;
        b.hiHz = std::min(spec.hiHz, rate * 0.5);
        b.firstBin = std::max(2, int(std::floor(b.loHz / b.binHz)) - 2);
        b.lastBin = std::min(b.n / 2 - 2, int(std::ceil(b.hiHz / b.binHz)) + 2);
        b.scale = 4.0f / float(b.n);  // periodic Hann: |X| = A * N / 4
        b.window.resize(size_t(b.n));
        for (int i = 0; i < b.n; ++i) b.window[size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * M_PI * i / b.n));
        b.frame.resize(size_t(b.n));
        b.re.resize(size_t(b.n / 2 + 1));
        b.im.resize(size_t(b.n / 2 + 1));
        b.mag.resize(size_t(b.n / 2 + 1));
        b.fft = createFft(spec.log2n);
        if (b.hiHz > b.loHz) bands_.push_back(std::move(b));
    }
    cands_.reserve(kMaxCandidates);
    scratch_.reserve(size_t(1 << 13));
}

void LineFrameAnalyzer::collect(Band& b, const float* samples, long count, int f) {
    const long start = long(f) * kHopSize - b.n / 2;
    bool any = false;
    for (int i = 0; i < b.n; ++i) {
        const long idx = start + i;
        const float s = idx >= 0 && idx < count ? samples[idx] : 0.0f;
        any = any || s != 0.0f;
        b.frame[size_t(i)] = s * b.window[size_t(i)];
    }
    if (!any) return;
    b.fft->forward(b.frame.data(), b.re.data(), b.im.data());
    scratch_.clear();
    for (int k = b.firstBin - 1; k <= b.lastBin + 1; ++k) {
        b.mag[size_t(k)] = std::sqrt(b.re[size_t(k)] * b.re[size_t(k)] + b.im[size_t(k)] * b.im[size_t(k)]) * b.scale;
        scratch_.push_back(b.mag[size_t(k)]);
    }
    const size_t mid = scratch_.size() / 2;
    std::nth_element(scratch_.begin(), scratch_.begin() + ptrdiff_t(mid), scratch_.end());
    const float floorLevel = std::max(kAbsoluteFloor, kMedianFactor * scratch_[mid]);

    const size_t firstOfBand = cands_.size();
    for (int k = b.firstBin; k <= b.lastBin; ++k) {
        const float m = b.mag[size_t(k)];
        if (m < floorLevel || !(m > b.mag[size_t(k - 1)]) || m < b.mag[size_t(k + 1)]) continue;
        const float a = std::log(std::max(b.mag[size_t(k - 1)], kTiny)), c = std::log(std::max(m, kTiny)), d = std::log(std::max(b.mag[size_t(k + 1)], kTiny));
        const float denom = a - 2.0f * c + d;
        const float shift = denom < -kTiny ? std::clamp(0.5f * (a - d) / denom, -0.5f, 0.5f) : 0.0f;
        const double hz = (double(k) + double(shift)) * b.binHz;
        if (hz < b.loHz || hz >= b.hiHz || hz < 20.0) continue;
        if (cands_.size() >= size_t(kMaxCandidates)) break;
        LinePoint p;
        p.midi = float(69.0 + 12.0 * std::log2(hz / 440.0));
        p.level = std::exp(c - 0.25f * (a - d) * shift);
        cands_.push_back(p);
    }
    // sidelobes of a strong partial are local maxima too: drop what the window's leakage explains
    for (size_t i = firstOfBand; i < cands_.size();) {
        bool leak = false;
        for (size_t j = firstOfBand; j < cands_.size() && !leak; ++j) {
            if (j == i || cands_[j].level <= cands_[i].level) continue;
            const double hzI = 440.0 * std::pow(2.0, (cands_[i].midi - 69.0) / 12.0), hzJ = 440.0 * std::pow(2.0, (cands_[j].midi - 69.0) / 12.0);
            const double d = std::fabs(hzI - hzJ) / b.binHz;
            if (d < kMinBinDistance) leak = true;
            else if (d <= kSidelobeBins) leak = cands_[i].level < cands_[j].level * kSidelobeBase * std::pow(kSidelobeRefBins / d, kSidelobeSlope);
        }
        if (leak) cands_.erase(cands_.begin() + ptrdiff_t(i));
        else ++i;
    }
}

int LineFrameAnalyzer::compute(const float* samples, long count, int f, LinePoint* out) {
    cands_.clear();
    for (Band& b : bands_) collect(b, samples, count, f);
    std::sort(cands_.begin(), cands_.end(), [](const LinePoint& a, const LinePoint& b) { return a.level > b.level; });
    if (cands_.size() > size_t(kRankedCandidates)) cands_.resize(size_t(kRankedCandidates));
    markHarmonics();
    // fundamentals first (a quiet line must not lose its slot to the overtones of a loud one), overtones fill the rest
    int n = 0, fundamentals = 0;
    for (const LinePoint& p : cands_)
        if (!(p.flags & kLineHarmonic) && fundamentals < kMaxFundamentals) { out[n++] = p; ++fundamentals; }
    for (const LinePoint& p : cands_)
        if ((p.flags & kLineHarmonic) && n < kLineSlots) out[n++] = p;
    return n;
}

// A candidate is an overtone when a clearly stronger, lower candidate has it at an integer frequency ratio 2..16.
void LineFrameAnalyzer::markHarmonics() {
    for (size_t i = 0; i < cands_.size(); ++i) {
        LinePoint& p = cands_[i];
        for (size_t j = 0; j < cands_.size() && !(p.flags & kLineHarmonic); ++j) {
            const LinePoint& q = cands_[j];
            if (j == i || q.midi >= p.midi || q.level * kHarmonicLevelRatio < p.level) continue;
            const double semis = double(p.midi) - double(q.midi);
            const int h = int(std::lround(std::exp2(semis / 12.0)));
            if (h < 2 || h > kMaxHarmonic) continue;
            const double error = std::fabs(semis - harmonicSemis(h));
            const bool isVeryHigh = h > kVeryHighHarmonic;
            if (error > (isVeryHigh ? kVeryHighHarmonicToleranceSemis : kHarmonicToleranceSemis)) continue;
            const float ratio = isVeryHigh ? kVeryHighHarmonicLevelRatio : h >= kHighHarmonic ? kHighHarmonicLevelRatio : 1.0f;
            if (p.level <= q.level * ratio) p.flags |= kLineHarmonic;
        }
    }
}

}  // namespace ttr
