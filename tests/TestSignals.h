// Synthetic test signals (also used by the host test and the UI screenshot tool).
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace tsig {

struct Tone { int pitch; double start, end; float amp; bool pure = false; };  // pure: sine even in saw mode

inline double midiToHz(int pitch) { return 440.0 * std::pow(2.0, (pitch - 69) / 12.0); }

// Band-limited additive tones with 10 ms fades. saw: partials 1/h up to 18 kHz.
inline std::vector<float> synth(const std::vector<Tone>& tones, double sr, double length, bool saw) {
    std::vector<float> out(size_t(length * sr), 0.0f);
    constexpr double kFade = 0.010;
    for (const Tone& t : tones) {
        const double f0 = midiToHz(t.pitch);
        const int partials = saw && !t.pure ? std::max(1, int(std::min(18000.0, sr * 0.45) / f0)) : 1;
        const size_t a = size_t(t.start * sr), b = std::min(out.size(), size_t(t.end * sr));
        for (size_t i = a; i < b; ++i) {
            const double time = double(i) / sr;
            const double env = std::min({1.0, (time - t.start) / kFade, (t.end - time) / kFade});
            double s = 0.0;
            for (int h = 1; h <= partials; ++h) s += std::sin(2.0 * M_PI * f0 * h * time) / h;
            out[i] += float(t.amp * std::max(0.0, env) * s * (saw && !t.pure ? 0.5 : 1.0));
        }
    }
    return out;
}

// A tone given in Hz (any tuning), optionally with vibrato. saw: partials 1/h up to 18 kHz.
struct HzTone { double hz, start, end; float amp; bool saw = false; double vibratoCents = 0.0, vibratoHz = 5.0, decay = 0.0; };  // decay: time constant of an exponential pluck envelope (0 = none)

inline std::vector<float> synthHz(const std::vector<HzTone>& tones, double sr, double length) {
    std::vector<float> out(size_t(length * sr), 0.0f);
    constexpr double kFade = 0.010;
    for (const HzTone& t : tones) {
        const int partials = t.saw ? std::max(1, int(std::min(18000.0, sr * 0.45) / (t.hz * 1.03))) : 1;
        const size_t a = size_t(t.start * sr), b = std::min(out.size(), size_t(t.end * sr));
        double phase = 0.0;
        for (size_t i = a; i < b; ++i) {
            const double time = double(i) / sr;
            const double cents = t.vibratoCents * std::sin(2.0 * M_PI * t.vibratoHz * (time - t.start));
            phase += 2.0 * M_PI * t.hz * std::pow(2.0, cents / 1200.0) / sr;
            double env = std::min({1.0, (time - t.start) / kFade, (t.end - time) / kFade});
            if (t.decay > 0.0) env *= std::exp(-(time - t.start) / t.decay);
            double s = 0.0;
            for (int h = 1; h <= partials; ++h) s += std::sin(phase * h) / h;
            out[i] += float(t.amp * std::max(0.0, env) * s * (t.saw ? 0.5 : 1.0));
        }
    }
    return out;
}

inline std::vector<Tone> chordThenMelody() {
    std::vector<Tone> t = {{57, 0.0, 2.0, 0.5f}, {61, 0.0, 2.0, 0.5f}, {64, 0.0, 2.0, 0.5f}};
    const int melody[] = {57, 59, 61, 64, 62, 60, 57};
    double at = 2.5;
    for (int p : melody) { t.push_back({p, at, at + 0.4, 0.5f}); at += 0.5; }
    return t;
}

}  // namespace tsig
