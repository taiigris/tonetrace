#include "analysis/BeatHits.h"

#include <algorithm>
#include <cmath>

#include "analysis/DrumFilter.h"

namespace ttr {

namespace {

constexpr double kBeforeSec = 0.045, kAfterSec = 0.060;  // as the drum gate: an analysis window sees a hit slightly before it happens
constexpr double kMinBeatSec = 0.40, kMaxBeatSec = 0.75, kBeatStepSec = 0.002;
constexpr int kLowRegisterNotes = 24;  // C1..B2
constexpr double kBinSec = 0.01;
constexpr int kSmoothBins = 4;           // +-40 ms
constexpr double kMinAlignedShare = 0.3; // a grid needs this share of the onset weight on its beats
constexpr double kMinBeatCoverage = 0.4;  // beats with an onset / all beats
constexpr int kMinBeats = 8;
constexpr double kHostPreference = 0.85; // the host grid is kept when it explains this much of the best grid's share

struct Onset { double time, weight; };

// Percussive onsets, weighted by how much low-register energy they add: the kick carries the beat, hats do not.
std::vector<Onset> onsetsOf(const Spectrogram& spec, float drumFilter) {
    std::vector<Onset> onsets;
    const std::vector<char> flags = percussiveFrames(spec, drumFilter, 0.0, 0.0);
    auto lowSum = [&](int f) {
        double sum = 0.0;
        for (int n = 0; n < kLowRegisterNotes; ++n) sum += double(spec.rawAt(f, n));
        return sum;
    };
    for (int f = 1; f < spec.frames; ++f) {
        if (!flags[size_t(f)]) continue;
        const double rise = lowSum(std::min(f + 1, spec.frames - 1)) - lowSum(f - 1);
        if (rise > 0.0) onsets.push_back({f * spec.hopSec, rise});
    }
    return onsets;
}

// Share of the onset weight within kBeatToleranceSec of the beats of (period, first).
double alignedShare(const std::vector<Onset>& onsets, double period, double first) {
    double total = 0.0, aligned = 0.0;
    for (const Onset& o : onsets) {
        total += o.weight;
        const double t = o.time - first;
        if (std::fabs(t - std::round(t / period) * period) <= kBeatToleranceSec) aligned += o.weight;
    }
    return total > 0.0 ? aligned / total : 0.0;
}

// Share of the beats (within the recording) that carry an onset: a kick pattern has one on nearly every beat, notes of a melody or
// bass line only meet a fitted grid by chance.
double beatCoverage(const std::vector<Onset>& onsets, const BeatGrid& grid, double lengthSec) {
    if (!grid.isValid() || lengthSec < 2.0 * grid.beatSec) return 0.0;
    const int beats = int((lengthSec - grid.firstBeatSec) / grid.beatSec);
    if (beats < kMinBeats) return 0.0;
    std::vector<char> hit(size_t(beats), 0);
    for (const Onset& o : onsets) {
        const double t = (o.time - grid.firstBeatSec) / grid.beatSec;
        const long k = std::lround(t);
        if (k >= 0 && k < beats && std::fabs(t - double(k)) * grid.beatSec <= kBeatToleranceSec) hit[size_t(k)] = 1;
    }
    return double(std::count(hit.begin(), hit.end(), char(1))) / double(beats);
}

BeatGrid ownGrid(const std::vector<Onset>& onsets, double* share) {
    BeatGrid best;
    *share = 0.0;
    double total = 0.0;
    for (const Onset& o : onsets) total += o.weight;
    if (total <= 0.0) return best;
    for (double period = kMinBeatSec; period <= kMaxBeatSec; period += kBeatStepSec) {
        const int bins = int(period / kBinSec) + 1;
        std::vector<double> fold(size_t(bins), 0.0);
        for (const Onset& o : onsets) fold[size_t(std::fmod(o.time, period) / kBinSec) % size_t(bins)] += o.weight;
        for (int b = 0; b < bins; ++b) {
            double sum = 0.0;
            for (int k = -kSmoothBins; k <= kSmoothBins; ++k) sum += fold[size_t((b + k + bins) % bins)];
            if (sum / total > *share) { *share = sum / total; best = {period, (b + 0.5) * kBinSec}; }
        }
    }
    return best;
}

}  // namespace

BeatGrid findBeatGrid(const Spectrogram& spec, double hostBpm, double hostStartPpq, float drumFilter) {
    BeatGrid host;
    if (hostBpm >= 40.0 && hostBpm <= 300.0) {
        const double beat = 60.0 / hostBpm;
        host = {beat, (std::ceil(hostStartPpq - 1e-9) - hostStartPpq) * beat};
    }
    const std::vector<Onset> onsets = onsetsOf(spec, drumFilter);
    double ownShare = 0.0;
    const BeatGrid own = ownGrid(onsets, &ownShare);
    const double hostShare = host.isValid() ? alignedShare(onsets, host.beatSec, host.firstBeatSec) : 0.0;
    const bool isHostClear = host.isValid() && beatCoverage(onsets, host, spec.lengthSec) >= kMinBeatCoverage;
    if (isHostClear && hostShare >= kHostPreference * ownShare) return host;
    return ownShare >= kMinAlignedShare && beatCoverage(onsets, own, spec.lengthSec) >= kMinBeatCoverage ? own : BeatGrid{};
}

std::vector<char> beatHitFrames(const Spectrogram& spec, const BeatGrid& grid, float drumFilter) {
    std::vector<char> frames(size_t(spec.frames), 0);
    if (!grid.isValid()) return frames;
    const std::vector<char> hits = percussiveFrames(spec, drumFilter, kBeforeSec, kAfterSec);
    for (int f = 0; f < spec.frames; ++f) {
        if (!hits[size_t(f)]) continue;
        const double t = f * spec.hopSec - grid.firstBeatSec;
        const double phase = std::fabs(t - std::round(t / grid.beatSec) * grid.beatSec);
        frames[size_t(f)] = phase <= kBeatToleranceSec + kBeforeSec ? 1 : 0;
    }
    return frames;
}

std::vector<char> beatFrames(const Spectrogram& spec, const BeatGrid& grid) {
    std::vector<char> frames(size_t(spec.frames), 0);
    if (!grid.isValid()) return frames;
    for (int f = 0; f < spec.frames; ++f) {
        const double t = f * spec.hopSec - grid.firstBeatSec;
        frames[size_t(f)] = std::fabs(t - std::round(t / grid.beatSec) * grid.beatSec) <= kBeatToleranceSec ? 1 : 0;
    }
    return frames;
}

bool isBeatPulse(const std::vector<float>& support, int first, int last, double hopSec, const BeatGrid& grid, int minBeats) {
    if (!grid.isValid()) return false;
    const double beat = grid.beatSec;
    const double t0 = first * hopSec, t1 = (last + 1) * hopSec;
    auto meanOver = [&](double a, double b) {
        double sum = 0.0;
        int count = 0;
        for (int f = std::max(first, int(std::ceil(a / hopSec))); f <= std::min(last, int(std::floor(b / hopSec))); ++f, ++count) sum += double(support[size_t(f)]);
        return count > 0 ? sum / count : 0.0;
    };
    std::vector<double> after, before;
    for (double t = grid.firstBeatSec + std::ceil((t0 - grid.firstBeatSec) / beat) * beat; t + beat <= t1; t += beat) {
        after.push_back(meanOver(t + 0.20 * beat, t + 0.45 * beat));
        before.push_back(meanOver(t + 0.65 * beat, t + 0.95 * beat));
    }
    if (int(after.size()) < minBeats) return false;
    // every beat is a pulse (strong after the beat, decayed before the next) and every beat re-excites the tone
    for (size_t k = 0; k < after.size(); ++k) {
        if (after[k] < kBeatPulseRatio * before[k]) return false;
        if (k > 0 && after[k] < kBeatReExcite * before[k - 1]) return false;
    }
    return true;
}

}  // namespace ttr
