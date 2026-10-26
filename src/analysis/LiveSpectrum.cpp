#include "analysis/LiveSpectrum.h"

#include <algorithm>

namespace ttr {

void LiveSpectrum::prepare(double sampleRate, double maxSeconds) {
    decimator_ = std::make_unique<Decimator>(sampleRate);
    builder_ = std::make_unique<NoteFrameBuilder>(decimator_->outputRate());
    const size_t decimatedCapacity = size_t(maxSeconds * decimator_->outputRate()) + size_t(kWindowSize);
    decimated_.assign(decimatedCapacity);
    maxFrames_ = int(decimatedCapacity / kHopSize);
    raw_.assign(size_t(maxFrames_) * kNumNotes);
    harmonic_.prepare(decimator_->outputRate(), maxFrames_);
    hopSec_ = double(kHopSize) / decimator_->outputRate();
    reset();
}

void LiveSpectrum::reset() {
    frames_.store(0, std::memory_order_release);
    peak_.store(0.0f);
    decimatedCount_ = 0;
    harmonic_.reset();
}

void LiveSpectrum::advance(const float* capture, size_t available) {
    if (!decimator_) return;
    const size_t factor = size_t(decimator_->factor());
    const size_t tail = size_t(decimator_->tail());
    // Output sample j reads capture samples up to j*factor + tail.
    const size_t ready = available > tail ? (available - tail - 1) / factor + 1 : 0;
    const size_t target = std::min(ready, decimated_.size());
    for (; decimatedCount_ < target; ++decimatedCount_)
        decimated_[decimatedCount_] = decimator_->at(capture, available, decimatedCount_);

    int frames = frames_.load(std::memory_order_relaxed);
    float peak = peak_.load(std::memory_order_relaxed);
    // Frame f needs decimated samples up to f*kHopSize + kWindowSize/2.
    while (frames < maxFrames_ && size_t(frames) * kHopSize + kWindowSize / 2 < decimatedCount_) {
        float* row = &raw_[size_t(frames) * kNumNotes];
        builder_->compute(decimated_.data(), long(decimatedCount_), frames, row);
        for (int n = 0; n < kNumNotes; ++n) peak = std::max(peak, row[n]);
        ++frames;
        frames_.store(frames, std::memory_order_release);
    }
    peak_.store(peak, std::memory_order_relaxed);
    harmonic_.advance(decimated_.data(), long(decimatedCount_), raw_.data(), frames);
}

}  // namespace ttr
