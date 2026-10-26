// Wait-free hand-over of events from the audio thread to the worker (single producer, single consumer).
// The audio thread must never log or allocate: it pushes a small POD record, the worker turns it into a log line.
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace ttr {

enum class AudioEventCode : uint8_t { None = 0, RecordStart, RecordStop, CaptureFull, BlockSize, OfflineChanged, FirstBlock };

struct AudioEvent {
    AudioEventCode code = AudioEventCode::None;
    int32_t a = 0;
    int32_t b = 0;
    double value = 0.0;
};

class AudioEventQueue {
public:
    // Audio thread. Drops the event when the queue is full.
    bool push(const AudioEvent& e) {
        const uint32_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= kSize) return false;
        slots_[h % kSize] = e;
        head_.store(h + 1, std::memory_order_release);
        return true;
    }
    // Consumer (worker or, when the worker is stopped, the thread that stopped it).
    bool pop(AudioEvent* e) {
        const uint32_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        *e = slots_[t % kSize];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr uint32_t kSize = 64;
    std::array<AudioEvent, kSize> slots_{};
    std::atomic<uint32_t> head_{0}, tail_{0};
};

}  // namespace ttr
