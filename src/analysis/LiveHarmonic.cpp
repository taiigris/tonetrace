#include "analysis/LiveHarmonic.h"

#include <algorithm>
#include <cmath>

#include "analysis/Hpss.h"
#include "analysis/Spectrogram.h"

namespace ttr {

void LiveHarmonic::prepare(double decimatedRate, int maxFrames) {
    builder_ = std::make_unique<TransientFrameBuilder>(decimatedRate);
    poolBins_ = builder_->bins();
    maxFrames_ = maxFrames;
    const double hopSec = double(kHopSize) / decimatedRate;
    timeWin_ = hpssOddWindow(int(std::lround(kHpssTimeSec / hopSec)));
    freqWin_ = hpssOddWindow(int(std::lround(kHpssFreqHz / builder_->binHz())));
    half_ = timeWin_ / 2;
    bins_ = noteToPooledBins(builder_->binHz(), poolBins_);
    pooled_.assign(size_t(maxFrames) * size_t(poolBins_));
    harmonic_.assign(size_t(maxFrames) * kNumNotes);
    harm_.assign(size_t(poolBins_), 0.0f);
    perc_.assign(size_t(poolBins_), 0.0f);
    scratch_.assign(size_t(std::max(timeWin_, freqWin_)), 0.0f);
    reset();
}

void LiveHarmonic::reset() {
    done_.store(0, std::memory_order_release);
    transientFrames_ = 0;
    globalMax_ = 0.0f;
}

void LiveHarmonic::advance(const float* decimated, long decimatedCount, const float* raw, int rawFrames) {
    if (!builder_) return;
    const int target = std::min(rawFrames, maxFrames_);
    for (; transientFrames_ < target; ++transientFrames_) {
        float* row = &pooled_[size_t(transientFrames_) * size_t(poolBins_)];
        builder_->compute(decimated, decimatedCount, transientFrames_, row);
        for (int b = 0; b < poolBins_; ++b) globalMax_ = std::max(globalMax_, row[b]);
    }
    int done = done_.load(std::memory_order_relaxed);
    const int finishable = transientFrames_ - half_;
    for (; done < finishable; ++done) finishFrame(done, transientFrames_, raw);
    done_.store(done, std::memory_order_release);
}

namespace {
float medianOf(float* v, int n) {
    std::nth_element(v, v + n / 2, v + n);
    return v[n / 2];
}
}  // namespace

// Harmonic frame g: time median over g-half..g+half (clamped at the start), frequency median within the frame.
void LiveHarmonic::finishFrame(int g, int available, const float* raw) {
    for (int b = 0; b < poolBins_; ++b) {
        for (int k = 0; k < timeWin_; ++k) {
            const int idx = std::min(available - 1, std::max(0, g + k - half_));
            scratch_[size_t(k)] = pooled_[size_t(idx) * size_t(poolBins_) + size_t(b)];
        }
        harm_[size_t(b)] = medianOf(scratch_.data(), timeWin_);
    }
    const float* row = &pooled_[size_t(g) * size_t(poolBins_)];
    const int fhalf = freqWin_ / 2;
    for (int b = 0; b < poolBins_; ++b) {
        for (int k = 0; k < freqWin_; ++k) scratch_[size_t(k)] = row[std::min(poolBins_ - 1, std::max(0, b + k - fhalf))];
        perc_[size_t(b)] = medianOf(scratch_.data(), freqWin_);
    }
    const float eps = (kHpssEpsRel * globalMax_) * (kHpssEpsRel * globalMax_);
    for (int b = 0; b < poolBins_; ++b) harm_[size_t(b)] = hpssMask(harm_[size_t(b)], perc_[size_t(b)], eps);  // now holds the mask
    for (int n = 0; n < kNumNotes; ++n) {
        const float mask = std::max(harm_[size_t(bins_[size_t(n)].first)], harm_[size_t(bins_[size_t(n)].second)]);
        harmonic_[size_t(g) * kNumNotes + size_t(n)] = raw[size_t(g) * kNumNotes + size_t(n)] * mask;
    }
}

}  // namespace ttr
