// Pre-allocated mono capture buffer. One writer (audio thread), readers on other threads.
#pragma once
#include <atomic>
#include <cstddef>
#include "core/LazyArray.h"

namespace ttr {

class CaptureBuffer {
public:
    void prepare(double sampleRate, double maxSeconds) {  // not real-time safe
        capacity_ = size_t(sampleRate * maxSeconds);
        data_.assign(capacity_);
        count_.store(0, std::memory_order_release);
    }
    void reset() { count_.store(0, std::memory_order_release); }
    // Not real-time safe: replaces the content (used when a saved recording is restored).
    void load(const float* samples, size_t count) {
        count_.store(0, std::memory_order_release);
        const size_t n = count < capacity_ ? count : capacity_;
        for (size_t i = 0; i < n; ++i) data_[i] = samples[i];
        count_.store(n, std::memory_order_release);
    }
    // Audio thread. Returns false once the buffer is full.
    bool write(const float* mono, size_t frames) {
        const size_t used = count_.load(std::memory_order_relaxed);
        const size_t n = frames < capacity_ - used ? frames : capacity_ - used;
        for (size_t i = 0; i < n; ++i) data_[used + i] = mono[i];
        count_.store(used + n, std::memory_order_release);
        return n == frames;
    }
    size_t size() const { return count_.load(std::memory_order_acquire); }
    size_t capacity() const { return capacity_; }
    const float* data() const { return data_.data(); }

private:
    LazyArray<float> data_;
    size_t capacity_ = 0;
    std::atomic<size_t> count_{0};
};

}  // namespace ttr
