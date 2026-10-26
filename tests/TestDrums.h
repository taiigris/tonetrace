// Synthetic drum kit for the hide-drums tests: kick, snare, hi-hat and a 4/4 pattern.
#pragma once
#include <cmath>
#include <cstdint>
#include <vector>

namespace tdrum {

struct Hit { double time; char kind; };  // 'k' kick, 's' snare, 'h' hat

class Noise {
public:
    float next() {  // deterministic white noise in [-1, 1)
        state_ = state_ * 1664525u + 1013904223u;
        return float(int32_t(state_)) / 2147483648.0f;
    }
private:
    uint32_t state_ = 12345u;
};

constexpr double kKickLen = 0.40, kSnareLen = 0.15, kHatLen = 0.040;

inline void addKick(std::vector<float>& out, double sr, double at, float amp) {
    double phase = 0.0;
    for (size_t i = 0; i < size_t(kKickLen * sr); ++i) {
        const double t = double(i) / sr;
        const double freq = 45.0 + 105.0 * std::exp(-t / 0.04);  // 150 -> 45 Hz
        phase += 2.0 * M_PI * freq / sr;
        const size_t idx = size_t(at * sr) + i;
        if (idx < out.size()) out[idx] += amp * float(std::exp(-t / 0.12) * std::sin(phase));
    }
}

inline void addSnare(std::vector<float>& out, double sr, double at, float amp, Noise& noise) {
    for (size_t i = 0; i < size_t(kSnareLen * sr); ++i) {
        const double t = double(i) / sr;
        const size_t idx = size_t(at * sr) + i;
        if (idx >= out.size()) break;
        const double tone = 0.5 * std::sin(2.0 * M_PI * 190.0 * t) * std::exp(-t / 0.05);
        out[idx] += amp * float(0.6 * noise.next() * std::exp(-t / 0.04) + tone);
    }
}

inline void addHat(std::vector<float>& out, double sr, double at, float amp, Noise& noise) {
    float prev = 0.0f, prev2 = 0.0f;
    for (size_t i = 0; i < size_t(kHatLen * sr); ++i) {
        const double t = double(i) / sr;
        const size_t idx = size_t(at * sr) + i;
        const float n = noise.next();
        const float hp = n - 2.0f * prev + prev2;  // second difference: high-pass
        prev2 = prev; prev = n;
        if (idx < out.size()) out[idx] += amp * 0.4f * hp * float(std::exp(-t / 0.012));
    }
}

// 4/4 at `bpm`: kick on every beat, snare on 2 and 4, hats on the off-beat 8ths.
inline std::vector<Hit> pattern(double bpm, double length) {
    std::vector<Hit> hits;
    const double beat = 60.0 / bpm;
    for (int b = 0; b * beat < length - 0.45; ++b) {
        hits.push_back({b * beat, 'k'});
        if (b % 2 == 1) hits.push_back({b * beat, 's'});
        hits.push_back({b * beat + beat / 2, 'h'});
    }
    return hits;
}

inline std::vector<float> render(const std::vector<Hit>& hits, double sr, double length, float level = 1.0f) {
    std::vector<float> out(size_t(length * sr), 0.0f);
    Noise noise;
    for (const Hit& h : hits) {
        if (h.kind == 'k') addKick(out, sr, h.time, 0.9f * level);
        else if (h.kind == 's') addSnare(out, sr, h.time, 0.7f * level, noise);
        else addHat(out, sr, h.time, 0.5f * level, noise);
    }
    return out;
}

// 808: sine with a click at the start and a slow decay.
inline std::vector<float> render808(double sr, double length, double hz, double noteLen) {
    std::vector<float> out(size_t(length * sr), 0.0f);
    Noise noise;
    for (size_t i = 0; i < size_t(noteLen * sr); ++i) {
        const double t = double(i) / sr;
        const double fade = std::min(1.0, std::min(t / 0.005, (noteLen - t) / 0.02));
        out[i] = float(0.7 * fade * std::exp(-t / 3.0) * std::sin(2.0 * M_PI * hz * t));
        if (t < 0.004) out[i] += 0.8f * noise.next();
    }
    return out;
}

}  // namespace tdrum
