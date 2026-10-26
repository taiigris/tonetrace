#include "analysis/WavePeaks.h"

#include <algorithm>

namespace ttr {

void PeakBuilder::prepare(double sampleRate, double maxSeconds) {
    samplesPerPeak_ = sampleRate / kPeaksPerSecond;
    minMax_.assign(size_t(maxSeconds * kPeaksPerSecond + 2) * 2);
    reset();
}

void PeakBuilder::reset() {
    count_.store(0, std::memory_order_release);
    consumed_ = 0;
    bin_ = 0;
    open_ = false;
}

void PeakBuilder::closeBin() {
    if (!open_ || size_t(bin_) * 2 + 1 >= minMax_.size()) return;
    minMax_[size_t(bin_) * 2] = lo_;
    minMax_[size_t(bin_) * 2 + 1] = hi_;
    ++bin_;
    open_ = false;
    count_.store(bin_, std::memory_order_release);
}

void PeakBuilder::advance(const float* samples, size_t available) {
    for (; consumed_ < available; ++consumed_) {
        const int target = int(double(consumed_) / samplesPerPeak_);
        if (open_ && target != bin_) closeBin();
        if (target != bin_) continue;  // beyond the preallocated range
        const float v = samples[consumed_];
        lo_ = open_ ? std::min(lo_, v) : v;
        hi_ = open_ ? std::max(hi_, v) : v;
        open_ = true;
    }
}

std::shared_ptr<WavePeaks> PeakBuilder::finish() const {
    auto peaks = std::make_shared<WavePeaks>();
    const int done = count();
    peaks->minMax.assign(minMax_.data(), minMax_.data() + ptrdiff_t(done) * 2);
    if (open_) {
        peaks->minMax.push_back(lo_);
        peaks->minMax.push_back(hi_);
    }
    return peaks;
}

}  // namespace ttr
