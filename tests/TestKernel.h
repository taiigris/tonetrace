// Helpers that drive a TonetraceKernel like a host would (shared by unit tests and the UI tools), plus fakes for the
// decoder and the model so the whole load -> transcribe -> play path runs without files or weights.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "analysis/ml/MuScriptorEngine.h"
#include "core/SongImport.h"
#include "core/TonetraceKernel.h"

namespace tk {

constexpr int kBlock = 512;
constexpr double kRate = 44100.0;

// Feeds `signal` (mono, duplicated to stereo) through the kernel in place and returns the left output channel.
inline std::vector<float> run(ttr::TonetraceKernel& k, const std::vector<float>& signal) {
    std::vector<float> out;
    std::vector<float> l(kBlock), r(kBlock);
    for (size_t pos = 0; pos + kBlock <= signal.size(); pos += kBlock) {
        std::copy(signal.begin() + ptrdiff_t(pos), signal.begin() + ptrdiff_t(pos + kBlock), l.begin());
        r = l;
        float* ch[2] = {l.data(), r.data()};
        k.process(ch, 2, kBlock);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

inline std::vector<float> silence(size_t blocks) { return std::vector<float>(blocks * kBlock, 0.0f); }

inline bool waitFor(const std::function<bool()>& cond, int ms = 8000) {
    for (int i = 0; i < ms / 5; ++i) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return cond();
}

inline double rms(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += double(x) * x;
    return std::sqrt(s / double(std::max<size_t>(1, v.size())));
}

inline std::vector<float> sine(double hz, double seconds, double rate = kRate, float amp = 0.4f) {
    std::vector<float> s(size_t(seconds * rate));
    for (size_t i = 0; i < s.size(); ++i) s[i] = amp * float(std::sin(2.0 * M_PI * hz * double(i) / rate));
    return s;
}

inline int16_t toInt16(float v) { return int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); }

// A decoder that "reads" a stereo sine of `seconds` for any path, and fails for paths containing "broken".
inline void installFakeDecoder(double seconds) {
    ttr::setAudioDecoder([seconds](const std::string& path, double rate, double maxSeconds, ttr::DecodedAudio* out, std::string* reason) {
        if (path.find("broken") != std::string::npos) { *reason = "the file is damaged"; return false; }
        const double used = std::min(seconds, maxSeconds);
        const std::vector<float> mono = sine(220.0, used, rate);
        out->samples.resize(mono.size() * 2);
        for (size_t i = 0; i < mono.size(); ++i) out->samples[2 * i] = out->samples[2 * i + 1] = toInt16(mono[i]);
        out->channels = 2;
        out->sampleRate = rate;
        out->fileSeconds = seconds;
        out->truncated = seconds > maxSeconds;
        return true;
    });
}

// A model that reports its notes in chunks. With `gated`, each chunk waits for release() (so a test can look at the
// document between chunks: playback while transcribing, notes arriving step by step).
class FakeTranscriber : public ttr::NoteTranscriber {
public:
    FakeTranscriber(std::vector<std::vector<ttr::TranscribedNote>> chunks, bool gated = false) : chunks_(std::move(chunks)), gated_(gated) {}
    bool transcribe(const std::vector<float>& audio, const ttr::ChunkCallback& onChunk, ttr::TaskControl* control, std::string* reason) override {
        samples = audio.size();
        for (size_t i = 0; i < chunks_.size(); ++i) {
            if (gated_) {
                std::unique_lock<std::mutex> lock(mutex_);
                while (!cv_.wait_for(lock, std::chrono::milliseconds(5), [&] { return released_ > int(i) || (control && control->cancelled()); })) {}
            }
            if (control && control->cancelled()) { *reason = "cancelled"; return false; }
            ttr::TranscriptionChunk chunk;
            chunk.newNotes = chunks_[i];
            chunk.finalizedSec = 5.0 * double(i + 1);
            chunk.progress = float(i + 1) / float(chunks_.size());
            onChunk(chunk);
            chunksDone.fetch_add(1);
        }
        return true;
    }
    void release() {
        std::lock_guard<std::mutex> lock(mutex_);
        ++released_;
        cv_.notify_all();
    }
    void releaseAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        released_ = 1 << 20;
        cv_.notify_all();
    }
    std::atomic<size_t> samples{0};
    std::atomic<int> chunksDone{0};

private:
    std::vector<std::vector<ttr::TranscribedNote>> chunks_;
    bool gated_;
    std::mutex mutex_;
    std::condition_variable cv_;
    int released_ = 0;
};

// Waits until the kernel's background job is done.
inline bool waitIdle(ttr::TonetraceKernel& k, int ms = 20000) { return waitFor([&] { return !k.isWorking(); }, ms); }

}  // namespace tk
