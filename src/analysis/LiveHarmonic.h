// Incremental harmonic/percussive separation for the live spectrogram ("Hide drums" while recording).
// Frame g is finished once the frames up to g + timeWin/2 exist, i.e. with a small fixed delay behind the raw spectrum.
// Same medians and mask as runHpss; the global maximum used for the mask epsilon is the running maximum.
#pragma once
#include <atomic>
#include <memory>
#include <vector>

#include "analysis/PooledBins.h"
#include "analysis/SpectralFrames.h"
#include "core/LazyArray.h"

namespace ttr {

class LiveHarmonic {
public:
    void prepare(double decimatedRate, int maxFrames);  // allocates (lazily committed)
    void reset();
    // `decimated`: the decimated signal so far; `raw`: note energies of the first `rawFrames` frames.
    void advance(const float* decimated, long decimatedCount, const float* raw, int rawFrames);

    int frames() const { return done_.load(std::memory_order_acquire); }  // finished harmonic frames
    const float* data() const { return harmonic_.data(); }                // frames * kNumNotes
    int delayFrames() const { return half_; }
    std::shared_ptr<const void> hold() const { return harmonic_.share(); }

private:
    void finishFrame(int g, int available, const float* raw);

    std::unique_ptr<TransientFrameBuilder> builder_;
    LazyArray<float> pooled_, harmonic_;
    std::vector<PooledPair> bins_;
    std::vector<float> column_, harm_, perc_, scratch_;
    int maxFrames_ = 0, poolBins_ = 0, timeWin_ = 9, freqWin_ = 11, half_ = 4;
    int transientFrames_ = 0;
    float globalMax_ = 0.0f;
    std::atomic<int> done_{0};
};

}  // namespace ttr
