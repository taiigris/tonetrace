// Counts malloc-family calls made by one registered thread (the "audio thread" of a test) via libmalloc's malloc_logger hook.
#pragma once
#include <pthread.h>

#include <atomic>
#include <cstdint>

extern "C" {
typedef void(malloc_logger_t)(uint32_t type, uintptr_t arg1, uintptr_t arg2, uintptr_t arg3, uintptr_t result, uint32_t num_hot_frames_to_skip);
extern malloc_logger_t* malloc_logger;
}

namespace tt {

class AllocCounter {
public:
    // Counts every allocation, reallocation and free made by the calling thread until stop().
    static void start() {
        count_.store(0);
        thread_.store(pthread_self());
        active_.store(true);
        malloc_logger = &AllocCounter::hook;
    }
    static long stop() {
        active_.store(false);
        malloc_logger = nullptr;
        return count_.load();
    }

private:
    static void hook(uint32_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uint32_t) {
        if (active_.load(std::memory_order_relaxed) && pthread_equal(pthread_self(), thread_.load(std::memory_order_relaxed))) count_.fetch_add(1, std::memory_order_relaxed);
    }
    static inline std::atomic<long> count_{0};
    static inline std::atomic<pthread_t> thread_{};
    static inline std::atomic<bool> active_{false};
};

}  // namespace tt
