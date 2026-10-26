// Pitch lines that grow while a recording is running (same analysis as the final one, without the finishing pass).
// The worker appends frames; the UI reads completed frames without a lock (storage is preallocated).
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>

#include "analysis/LineFrames.h"
#include "analysis/LineTracker.h"
#include "core/LazyArray.h"

namespace ttr {

class LiveLines {
public:
    void prepare(double decimatedRate, int maxFrames);  // allocates
    void reset();
    // Appends every frame the first `count` decimated samples allow.
    void advance(const float* decimated, size_t count);

    int frames() const { return frames_.load(std::memory_order_acquire); }
    const LinePoint* points() const { return points_.data(); }
    const uint8_t* counts() const { return counts_.data(); }
    float peak() const { return peak_.load(std::memory_order_relaxed); }
    std::shared_ptr<const void> hold() const { return points_.share(); }

private:
    std::unique_ptr<LineFrameAnalyzer> analyzer_;
    LineTracker tracker_;
    LazyArray<LinePoint> points_;
    LazyArray<uint8_t> counts_;
    int maxFrames_ = 0;
    std::atomic<int> frames_{0};
    std::atomic<float> peak_{0.0f};
};

}  // namespace ttr
