// Synthetic techno mixes with ground truth: four-on-the-floor kick (sine sweep 150 -> 45 Hz), sidechain pumping of bass and
// pad (8..12 dB), hi-hats, held bass (F1 / C2 ...), chord pad and short melody stabs, 125..135 BPM.
// Also used by tools/accuracy.cpp (accuracy table in docs/ACCURACY.md).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "TestDrums.h"

namespace ttech {

struct Truth { int pitch; double start, end; };

struct Mix {
    std::string name;
    double bpm = 130.0;
    double length = 0.0;
    std::vector<float> audio;
    std::vector<Truth> truth;
};

struct Recipe {
    const char* name;
    double bpm;
    int bars;
    float duckDb;      // sidechain depth on bass and pad
    int bassSeed;      // chooses the bass line
    int chordRows;     // which chord progression
    int stabEvery;     // a stab on every n-th 1/8 slot (pseudo random)
};

inline std::vector<Recipe> recipes() {
    return {{"techno-125", 125.0, 12, 8.0f, 0, 0, 3}, {"techno-128", 128.0, 12, 10.0f, 1, 1, 2},
            {"techno-130", 130.0, 12, 12.0f, 2, 2, 3}, {"techno-135", 135.0, 12, 9.0f, 3, 3, 2}};
}

namespace detail {

constexpr double kRate = 44100.0;
constexpr double kFade = 0.010;

struct Lcg {
    uint32_t state;
    explicit Lcg(uint32_t seed) : state(2463534242u + seed * 977u) {}
    double next() { state = state * 1664525u + 1013904223u; return double(state >> 8) / double(1u << 24); }
};

inline double midiHz(int pitch) { return 440.0 * std::pow(2.0, (pitch - 69) / 12.0); }

// Sum of partials 1/h (h <= maxPartials), 10 ms fades, optional gain envelope (sidechain).
inline void addTone(std::vector<float>& out, int pitch, double start, double end, float amp, int maxPartials, const std::vector<float>* duck) {
    const double f0 = midiHz(pitch);
    const size_t a = size_t(start * kRate), b = std::min(out.size(), size_t(end * kRate));
    const int partials = std::max(1, std::min(maxPartials, int(9000.0 / f0)));
    for (size_t i = a; i < b; ++i) {
        const double t = double(i) / kRate;
        const double env = std::max(0.0, std::min({1.0, (t - start) / kFade, (end - t) / kFade}));
        double s = 0.0;
        for (int h = 1; h <= partials; ++h) s += std::sin(2.0 * M_PI * f0 * h * t) / h;
        out[i] += amp * float(env * s) * (duck ? (*duck)[i] : 1.0f);
    }
}

inline std::vector<float> duckGain(double bpm, double length, float depthDb) {
    std::vector<float> gain(size_t(length * kRate), 1.0f);
    const double beat = 60.0 / bpm;
    constexpr double kRelease = 0.30;
    for (size_t i = 0; i < gain.size(); ++i) {
        const double since = std::fmod(double(i) / kRate, beat);
        const double dip = since < kRelease ? std::pow(1.0 - since / kRelease, 1.5) : 0.0;
        gain[i] = float(std::pow(10.0, -depthDb * dip / 20.0));
    }
    return gain;
}

inline void addKickSweep(std::vector<float>& out, double at, float amp) {
    double phase = 0.0;
    for (size_t i = 0; i < size_t(0.40 * kRate); ++i) {
        const double t = double(i) / kRate;
        phase += 2.0 * M_PI * (45.0 + 105.0 * std::exp(-t / 0.04)) / kRate;
        const size_t idx = size_t(at * kRate) + i;
        if (idx < out.size()) out[idx] += amp * float(std::exp(-t / 0.12) * std::sin(phase));
    }
}

}  // namespace detail

inline Mix build(const Recipe& r) {
    using namespace detail;
    Mix mix;
    mix.name = r.name;
    mix.bpm = r.bpm;
    const double beat = 60.0 / r.bpm, bar = 4.0 * beat;
    mix.length = r.bars * bar + 0.3;
    mix.audio.assign(size_t(mix.length * kRate), 0.0f);
    std::vector<float> bass(mix.audio.size(), 0.0f), pad(mix.audio.size(), 0.0f), top(mix.audio.size(), 0.0f), drums(mix.audio.size(), 0.0f);
    const std::vector<float> duck = duckGain(r.bpm, mix.length, r.duckDb);
    Lcg rng(uint32_t(r.bassSeed) + 11u);

    // Bass: held notes of 1..2 bars (a few of one beat), F1 / C2 and neighbours.
    static const int kBassPool[4][5] = {{29, 36, 34, 31, 29}, {34, 29, 36, 32, 34}, {31, 36, 29, 27, 31}, {29, 33, 36, 34, 29}};
    double t = 0.0;
    int k = 0;
    while (t < r.bars * bar - 0.5 * beat) {
        const double len = (rng.next() < 0.25 ? 1.0 : (rng.next() < 0.5 ? 4.0 : 8.0)) * beat;
        const double end = std::min(t + len - 0.04, r.bars * bar - 0.05);
        const int pitch = kBassPool[r.bassSeed % 4][k++ % 5];
        mix.truth.push_back({pitch, t, end});
        addTone(bass, pitch, t, end, 0.30f, 6, &duck);
        t += len;
    }

    // Pad: three-note chords held for two bars.
    static const int kChords[4][3][3] = {{{53, 56, 60}, {51, 55, 58}, {49, 53, 56}}, {{50, 53, 57}, {48, 52, 55}, {46, 50, 53}},
                                         {{56, 60, 63}, {53, 56, 60}, {51, 55, 58}}, {{55, 58, 62}, {53, 57, 60}, {50, 53, 57}}};
    for (int c = 0; c < r.bars / 2; ++c) {
        const double a = c * 2.0 * bar + 0.02, b = (c + 1) * 2.0 * bar - 0.04;
        for (int pitch : kChords[r.chordRows % 4][c % 3]) {
            mix.truth.push_back({pitch, a, b});
            addTone(pad, pitch, a, b, 0.07f, 10, &duck);
        }
    }

    // Stabs: 1/8 or 3/16 notes on pseudo random 1/8 slots of the upper register.
    static const int kScale[8] = {65, 68, 70, 72, 75, 77, 80, 82};
    for (int slot = 2; slot < r.bars * 8 - 2; ++slot) {
        if (int(rng.next() * r.stabEvery) != 0) continue;
        const double a = slot * beat / 2.0 + 0.01;
        const double len = (rng.next() < 0.7 ? 0.5 : 0.75) * beat - 0.03;
        const int pitch = kScale[int(rng.next() * 8.0) & 7];
        mix.truth.push_back({pitch, a, a + len});
        addTone(top, pitch, a, a + len, 0.10f, 8, nullptr);
        slot += int(len / (beat / 2.0));  // monophonic
    }

    // Drums: kick on every beat, hats on the off-beat 1/8 and 1/16.
    tdrum::Noise noise;
    for (int b = 0; (b + 1) * beat < mix.length - 0.4; ++b) {
        addKickSweep(drums, b * beat, 0.8f);
        tdrum::addHat(drums, kRate, b * beat + beat / 2.0, 0.5f, noise);
        tdrum::addHat(drums, kRate, b * beat + beat * 0.75, 0.25f, noise);
    }
    for (size_t i = 0; i < mix.audio.size(); ++i) mix.audio[i] = bass[i] + pad[i] + top[i] + drums[i];
    std::sort(mix.truth.begin(), mix.truth.end(), [](const Truth& a, const Truth& b) { return a.start != b.start ? a.start < b.start : a.pitch < b.pitch; });
    return mix;
}

}  // namespace ttech
