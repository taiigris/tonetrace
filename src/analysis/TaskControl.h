// Cancel flag and progress of a long-running background computation (Quick Grab).
#pragma once
#include <atomic>

namespace ttr {

struct TaskControl {
    std::atomic<bool> cancel{false};
    std::atomic<float> progress{0.0f};  // 0..1
    bool cancelled() const { return cancel.load(std::memory_order_relaxed); }
    void report(float p) { progress.store(p, std::memory_order_relaxed); }
};

}  // namespace ttr
