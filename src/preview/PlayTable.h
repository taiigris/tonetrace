// Lock-free hand-over of the notes to play (worker/UI thread -> audio thread): two buffers allocated once, the audio
// thread reads one while the other is rewritten. Notes are sorted by start and carry their instrument's gain.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace ttr {

struct PlayNote {
    double startSec = 0.0, endSec = 0.0;
    float gain = 0.0f;  // instrument fader (muted instruments are left out when published)
    int16_t pitch = 60, program = -1;
};

class PlayTable {
public:
    static constexpr int kMaxNotes = 1 << 17;    // 131072: a 10 minute song has around 20000
    static constexpr double kMaxNoteSec = 30.0;  // a note sounding at a seek position started at most this long before

    PlayTable() {
        for (Buffer& b : buffers_) b.notes.assign(size_t(kMaxNotes), PlayNote{});
    }
    // Non-audio threads (serialised). Waits (bounded) while the audio thread still reads the target buffer.
    void publish(const std::vector<PlayNote>& notes) {
        std::lock_guard<std::mutex> guard(publishMutex_);
        const int target = 1 - active_.load();
        for (int i = 0; i < 500 && busy_.load() == target; ++i) std::this_thread::sleep_for(std::chrono::microseconds(200));
        Buffer& b = buffers_[target];
        b.count = int(std::min<size_t>(notes.size(), size_t(kMaxNotes)));
        std::copy(notes.begin(), notes.begin() + b.count, b.notes.begin());
        b.generation = ++generation_;
        active_.store(target);
    }

    struct View {
        const PlayNote* notes = nullptr;
        int count = 0;
        uint64_t generation = 0;
        // Index of the first note starting at or after `sec`.
        int firstFrom(double sec) const {
            const PlayNote* it = std::lower_bound(notes, notes + count, sec, [](const PlayNote& n, double t) { return n.startSec < t; });
            return int(it - notes);
        }
    };
    // Audio thread: acquire/release pair around one render block.
    View acquire() {
        int idx;
        do { idx = active_.load(); busy_.store(idx); } while (active_.load() != idx);
        const Buffer& b = buffers_[idx];
        return {b.notes.data(), b.count, b.generation};
    }
    void release() { busy_.store(-1); }

private:
    struct Buffer {
        std::vector<PlayNote> notes;
        int count = 0;
        uint64_t generation = 0;
    };
    Buffer buffers_[2];
    std::atomic<int> active_{0};
    std::atomic<int> busy_{-1};
    std::mutex publishMutex_;
    uint64_t generation_ = 0;
};

}  // namespace ttr
