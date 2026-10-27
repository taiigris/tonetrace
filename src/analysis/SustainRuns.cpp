#include "analysis/SustainRuns.h"

#include <algorithm>

namespace ttr {

std::vector<Run> hysteresisRuns(const std::vector<float>& v, float on, float off) {
    std::vector<Run> runs;
    const int frames = int(v.size());
    int f = 0;
    while (f < frames) {
        if (v[size_t(f)] < on) { ++f; continue; }
        int a = f, b = f;
        while (a > 0 && v[size_t(a - 1)] >= off) --a;
        while (b + 1 < frames && v[size_t(b + 1)] >= off) ++b;
        runs.push_back({std::max(a, runs.empty() ? 0 : runs.back().last + 1), b});
        f = b + 1;
    }
    return runs;
}

namespace {

// True when the support jumps up by kAttackRise within two frames somewhere in [from, to].
bool hasAttack(const std::vector<float>& support, int from, int to) {
    for (int f = from; f <= to && f + 2 < int(support.size()); ++f)
        if (support[size_t(f + 2)] >= kAttackRise * std::max(support[size_t(f)], 1e-9f)) return true;
    return false;
}

// A gap [from, from + length) is a tone masked by a hit when it is short, starts at a beat hit and the tone never fell silent in it.
bool isMasked(const std::vector<char>& beatHits, const std::vector<float>& support, int from, int length, int longBridge, float reference) {
    if (length > longBridge || *std::min_element(support.begin() + from, support.begin() + from + length) < kSilenceRatio * reference) return false;
    for (int f = std::max(0, from - kMaskLeadFrames); f <= from + kMaskLeadFrames && f < int(beatHits.size()); ++f)
        if (beatHits[size_t(f)]) return true;
    return false;
}

struct SplitContext {
    const std::vector<float>& v;
    const std::vector<float>& support;
    const std::vector<float>& rawSupport;  // what the pitch does underneath a drum hit
    const std::vector<char>& beatHits;
    int longBridge;
};

void splitAtValleys(const SplitContext& c, Run run, std::vector<Run>* out) {
    const std::vector<float>& v = c.v;
    const std::vector<float>& support = c.support;
    if (run.last < run.first) return;
    std::vector<int> frames;  // the interior frames, deepest first
    for (int f = run.first + 1; f < run.last; ++f) frames.push_back(f);
    std::sort(frames.begin(), frames.end(), [&](int a, int b) { return v[size_t(a)] != v[size_t(b)] ? v[size_t(a)] < v[size_t(b)] : a < b; });
    for (const int valley : frames) {
        const float left = *std::max_element(v.begin() + run.first, v.begin() + valley + 1);
        const float right = *std::max_element(v.begin() + valley, v.begin() + run.last + 1);
        const float flank = std::min(left, right);
        const bool isDeep = v[size_t(valley)] < kValleyRatio * flank;
        const float limit = (isDeep ? kValleyRatio : kAttackValleyRatio) * flank;
        if (v[size_t(valley)] >= limit) continue;
        const float flankRaw = std::min(*std::max_element(c.rawSupport.begin() + run.first, c.rawSupport.begin() + valley + 1), *std::max_element(c.rawSupport.begin() + valley, c.rawSupport.begin() + run.last + 1));
        int a = valley, b = valley;
        while (a > run.first && v[size_t(a - 1)] < limit) --a;
        while (b < run.last && v[size_t(b + 1)] < limit) ++b;
        if (isMasked(c.beatHits, c.rawSupport, a, b - a + 1, c.longBridge, flankRaw)) continue;       // a kick on the beat pulled the tone down
        if (!isDeep && !hasAttack(support, std::max(a, b - 2), b)) continue;  // a new attack leaves the valley
        splitAtValleys(c, {run.first, a - 1}, out);
        splitAtValleys(c, {b + 1, run.last}, out);
        return;
    }
    out->push_back(run);
}

float peakOf(const std::vector<float>& v, const Run& r) { return *std::max_element(v.begin() + r.first, v.begin() + r.last + 1); }

}  // namespace

std::vector<Run> valleyRuns(const std::vector<float>& v, const std::vector<float>& support, const std::vector<float>& rawSupport, const std::vector<char>& beatHits, int longBridge, const std::vector<Run>& runs) {
    std::vector<Run> out;
    const SplitContext c{v, support, rawSupport, beatHits, longBridge};
    for (const Run& r : runs) splitAtValleys(c, r, &out);
    return out;
}

std::vector<Run> bridgeRuns(const std::vector<float>& support, const std::vector<float>& attackSupport, const std::vector<float>& maskSupport, const std::vector<char>& beatHits, const std::vector<Run>& runs, int shortBridge, int longBridge, int heldThroughMax) {
    std::vector<Run> out;
    for (const Run& r : runs) {
        if (!out.empty()) {
            const int a = out.back().last + 1, b = r.first - 1;
            const int gap = b - a + 1;
            const float valley = gap > 0 ? *std::min_element(support.begin() + a, support.begin() + b + 1) : 1.0f;
            const bool isShallow = valley >= kValleyRatio * std::min(peakOf(support, out.back()), peakOf(support, r)) && !hasAttack(attackSupport, std::max(a, b - 2), r.first);
            const bool isHeldThrough = gap <= heldThroughMax && valley >= kHeldThroughRatio * std::min(peakOf(support, out.back()), peakOf(support, r));
            if (gap <= shortBridge || isHeldThrough || (gap <= longBridge && (isShallow || isMasked(beatHits, maskSupport, a, gap, longBridge, std::min(peakOf(maskSupport, out.back()), peakOf(maskSupport, r)))))) { out.back().last = r.last; continue; }
        }
        out.push_back(r);
    }
    return out;
}

Run trimRun(const std::vector<float>& v, Run run, float ratio) {
    const float peak = peakOf(v, run);
    while (run.first < run.last && v[size_t(run.first)] < ratio * peak) ++run.first;
    while (run.last > run.first && v[size_t(run.last)] < ratio * peak) --run.last;
    return run;
}

}  // namespace ttr
