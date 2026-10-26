// Helpers that drive a TonetraceKernel like a host would (shared by unit tests and the UI tools).
#pragma once
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

#include "core/TonetraceKernel.h"

namespace tk {

constexpr int kBlock = 512;

// Feeds `signal` (mono, duplicated to stereo) through the kernel in place and returns the left output channel.
inline std::vector<float> run(ttr::TonetraceKernel& k, const std::vector<float>& signal, size_t from = 0, size_t to = size_t(-1)) {
    to = std::min(to, signal.size());
    std::vector<float> out;
    std::vector<float> l(kBlock), r(kBlock);
    for (size_t pos = from; pos + kBlock <= to; pos += kBlock) {
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
    for (int i = 0; i < ms / 10; ++i) {
        if (cond()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return cond();
}

// Record on, play `signal`, record off, wait for the analysis. Returns false when it did not finish.
inline bool record(ttr::TonetraceKernel& k, const std::vector<float>& signal) {
    k.setParam(ttr::kParamRecord, 1.0f);
    run(k, signal);
    k.setParam(ttr::kParamRecord, 0.0f);
    run(k, silence(2));
    return waitFor([&] { return k.status() == ttr::KernelStatus::Idle && k.snapshot() && k.snapshot()->hasRecording(); });
}

inline double rms(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += double(x) * x;
    return std::sqrt(s / double(std::max<size_t>(1, v.size())));
}

}  // namespace tk

namespace tk {

// Block sizes a host may use: single samples, odd sizes, powers of two, the maximum and 0 (parameter flush only).
inline const std::vector<int>& variableBlocks() {
    static const std::vector<int> sizes = {1, 2, 3, 7, 64, 127, 0, 480, 512, 1023, 333, 4096, 0, 64, 5};
    return sizes;
}

struct Rendered {
    std::vector<float> left, right;
    size_t fed = 0;
};

// Feeds `signal` (right channel = 0.5 * left; mono when channels == 1) in blocks of the given sizes, cycling through them.
inline Rendered runBlocks(ttr::TonetraceKernel& k, const std::vector<float>& signal, const std::vector<int>& sizes, int channels = 2, size_t startIndex = 0) {
    Rendered out;
    std::vector<float> l(4096), r(4096);
    size_t pos = 0, turn = startIndex;
    while (pos < signal.size()) {
        const int frames = std::min<size_t>(size_t(sizes[turn++ % sizes.size()]), signal.size() - pos);
        for (int i = 0; i < frames; ++i) { l[size_t(i)] = signal[pos + size_t(i)]; r[size_t(i)] = 0.5f * signal[pos + size_t(i)]; }
        float* ch[2] = {l.data(), r.data()};
        k.process(ch, channels, frames);
        out.left.insert(out.left.end(), l.begin(), l.begin() + frames);
        out.right.insert(out.right.end(), r.begin(), r.begin() + frames);
        pos += size_t(frames);
    }
    out.fed = pos;
    return out;
}

inline bool bitEqual(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

}  // namespace tk
