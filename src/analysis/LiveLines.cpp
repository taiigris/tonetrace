#include "analysis/LiveLines.h"

#include <algorithm>

#include "analysis/SpectralFrames.h"

namespace ttr {

void LiveLines::prepare(double decimatedRate, int maxFrames) {
    analyzer_ = std::make_unique<LineFrameAnalyzer>(decimatedRate);
    maxFrames_ = maxFrames;
    points_.assign(size_t(maxFrames) * kLineSlots);
    counts_.assign(size_t(maxFrames));
    reset();
}

void LiveLines::reset() {
    frames_.store(0, std::memory_order_release);
    peak_.store(0.0f);
    tracker_.reset();
}

void LiveLines::advance(const float* decimated, size_t count) {
    if (!analyzer_ || !decimated) return;
    int frames = frames_.load(std::memory_order_relaxed);
    float peak = peak_.load(std::memory_order_relaxed);
    const size_t ahead = size_t(LineFrameAnalyzer::lookahead());
    while (frames < maxFrames_ && size_t(frames) * kHopSize + ahead <= count) {
        LinePoint* row = &points_[size_t(frames) * kLineSlots];
        const int n = analyzer_->compute(decimated, long(count), frames, row);
        tracker_.assign(frames, row, n);
        for (int i = 0; i < n; ++i) peak = std::max(peak, row[i].level);
        counts_[size_t(frames)] = uint8_t(n);
        ++frames;
        peak_.store(peak, std::memory_order_relaxed);  // before the frame count: a reader that sees the frame sees its level
        frames_.store(frames, std::memory_order_release);
    }
    peak_.store(peak, std::memory_order_relaxed);
}

}  // namespace ttr
