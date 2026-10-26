// Spectrogram that grows while a recording is running. The analysis thread appends frames as soon as enough
// audio has arrived; the UI reads the completed frames without any lock (storage is preallocated).
#pragma once
#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "analysis/LiveHarmonic.h"
#include "analysis/SpectralFrames.h"
#include "core/LazyArray.h"

namespace ttr {

class LiveSpectrum {
public:
    void prepare(double sampleRate, double maxSeconds);  // allocates
    void reset();
    // Appends every frame that the first `available` capture samples (host rate) allow.
    void advance(const float* capture, size_t available);

    int frames() const { return frames_.load(std::memory_order_acquire); }
    const float* raw() const { return raw_.data(); }
    double hopSec() const { return hopSec_; }
    float peak() const { return peak_.load(std::memory_order_relaxed); }
    // Harmonic part (Hide drums): finished frames lag a few frames behind frames().
    const float* harmonic() const { return harmonic_.data(); }
    int harmonicFrames() const { return harmonic_.frames(); }
    int harmonicDelay() const { return harmonic_.delayFrames(); }
    std::shared_ptr<const void> rawHold() const { return raw_.share(); }
    std::shared_ptr<const void> harmonicHold() const { return harmonic_.hold(); }

private:
    std::unique_ptr<Decimator> decimator_;
    std::unique_ptr<NoteFrameBuilder> builder_;
    LazyArray<float> decimated_, raw_;
    LiveHarmonic harmonic_;
    size_t decimatedCount_ = 0;
    int maxFrames_ = 0;
    double hopSec_ = 0.0;
    std::atomic<int> frames_{0};
    std::atomic<float> peak_{0.0f};
};

}  // namespace ttr
