// Lock-free hand-over of the selected notes (UI thread -> audio thread) plus a tiny audition queue.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace ttr {

struct PreviewNote {
    float startSec, endSec, velocity;
    int pitch;
};

class NoteTable {
public:
    static constexpr int kMaxNotes = 8192;
    struct View {
        const PreviewNote* notes;
        int count;
        double loopSec;
    };

    // Non-audio thread. Waits (bounded) while the audio thread still reads the target buffer.
    void publish(const PreviewNote* notes, int count, double loopSec) {
        const int target = 1 - active_.load();
        for (int i = 0; i < 200 && busy_.load() == target; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Buffer& b = buffers_[target];
        b.count = count < kMaxNotes ? count : kMaxNotes;
        for (int i = 0; i < b.count; ++i) b.notes[i] = notes[i];
        b.loopSec = loopSec;
        active_.store(target);
    }
    // Audio thread: acquire/release pair around one render block.
    View acquire() {
        int idx;
        do { idx = active_.load(); busy_.store(idx); } while (active_.load() != idx);
        return {buffers_[idx].notes.data(), buffers_[idx].count, buffers_[idx].loopSec};
    }
    void release() { busy_.store(-1); }

    // Audition queue (single producer UI, single consumer audio).
    void pushAudition(int pitch) {
        const unsigned h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) >= kQueue) return;
        queue_[h % kQueue] = pitch;
        head_.store(h + 1, std::memory_order_release);
    }
    bool popAudition(int* pitch) {
        const unsigned t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_acquire)) return false;
        *pitch = queue_[t % kQueue];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

private:
    static constexpr unsigned kQueue = 16;
    struct Buffer {
        std::array<PreviewNote, kMaxNotes> notes;
        int count = 0;
        double loopSec = 0.0;
    };
    Buffer buffers_[2];
    std::atomic<int> active_{0};
    std::atomic<int> busy_{-1};
    std::array<int, kQueue> queue_{};
    std::atomic<unsigned> head_{0}, tail_{0};
};

}  // namespace ttr
