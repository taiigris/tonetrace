// Incremental waveform overview: one min/max pair per 1/kPeaksPerSecond. Single writer (analysis thread),
// readers on other threads only look at the `count()` completed pairs.
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "core/LazyArray.h"
#include "core/Types.h"

namespace ttr {

class PeakBuilder {
public:
    void prepare(double sampleRate, double maxSeconds);  // allocates
    void reset();
    // Consumes samples [consumed, available) of the capture buffer.
    void advance(const float* samples, size_t available);
    int count() const { return count_.load(std::memory_order_acquire); }
    const float* data() const { return minMax_.data(); }
    std::shared_ptr<const void> hold() const { return minMax_.share(); }
    // Everything consumed so far, including the partially filled last pair.
    std::shared_ptr<WavePeaks> finish() const;

private:
    void closeBin();
    double samplesPerPeak_ = 1.0;
    LazyArray<float> minMax_;
    size_t consumed_ = 0;
    int bin_ = 0;
    float lo_ = 0.0f, hi_ = 0.0f;
    bool open_ = false;
    std::atomic<int> count_{0};
};

}  // namespace ttr
