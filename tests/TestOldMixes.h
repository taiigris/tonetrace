// The synthetic mixes of the v3 comparison (melody, chords, bass, vibrato, detune, noise, drums, full mix); shared by tools/compare_v3.cpp and tools/accuracy.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "TestDrums.h"
#include "TestSignals.h"

namespace oldmix {

constexpr double kSampleRate = 44100.0;

struct Truth { int pitch; double start, end; };
struct Mix { std::string name; std::vector<float> audio; std::vector<Truth> truth; };

// Small deterministic generator, independent of the platform's rand().
struct Lcg {
    uint32_t state = 2463534242u;
    double next() { state = state * 1664525u + 1013904223u; return double(state >> 8) / double(1u << 24); }
};

void addInto(std::vector<float>& dst, const std::vector<float>& src, float gain = 1.0f) {
    for (size_t i = 0; i < dst.size() && i < src.size(); ++i) dst[i] += gain * src[i];
}

std::vector<float> renderTruth(const std::vector<Truth>& notes, double centsOff, double length, bool saw, float amp, double vibratoCents) {
    std::vector<tsig::HzTone> tones;
    for (const Truth& n : notes)
        tones.push_back({tsig::midiToHz(n.pitch) * std::pow(2.0, centsOff / 1200.0), n.start, n.end, amp, saw, vibratoCents, 5.0, 0.0});
    return tsig::synthHz(tones, kSampleRate, length);
}

std::vector<Truth> melody(Lcg& rng, double length, int lo, int hi, double minLen, double maxLen) {
    std::vector<Truth> notes;
    int pitch = (lo + hi) / 2;
    for (double t = 0.2; t < length - maxLen - 0.2;) {
        const double len = minLen + (maxLen - minLen) * rng.next();
        pitch = std::clamp(pitch + int(rng.next() * 9.0) - 4, lo, hi);
        notes.push_back({pitch, t, t + len});
        t += len + 0.05 + 0.1 * rng.next();
    }
    return notes;
}

std::vector<Truth> chords(double length) {
    const int roots[] = {57, 53, 48, 55};
    std::vector<Truth> notes;
    int k = 0;
    for (double t = 0.2; t < length - 2.0; t += 2.0, ++k)
        for (int offset : {0, 4, 7}) notes.push_back({roots[k % 4] + offset, t, t + 1.8});
    return notes;
}

std::vector<float> drumTrack(double length) {
    return tdrum::render(tdrum::pattern(120.0, length), kSampleRate, length, 0.8f);
}

void addNoise(std::vector<float>& audio, float level) {
    tdrum::Noise noise;
    for (float& v : audio) v += level * noise.next();
}

std::vector<Mix> buildMixes() {
    constexpr double kLen = 14.0;
    std::vector<Mix> mixes;
    auto make = [&](const char* name, const std::vector<Truth>& truth, double cents, float amp, double vibrato) {
        Mix m;
        m.name = name;
        m.truth = truth;
        m.audio = renderTruth(truth, cents, kLen, true, amp, vibrato);
        return m;
    };
    Lcg rng;
    const std::vector<Truth> lead = melody(rng, kLen, 60, 76, 0.25, 0.6);
    const std::vector<Truth> bass = melody(rng, kLen, 36, 47, 0.4, 0.9);
    mixes.push_back(make("melody", lead, 0.0, 0.4f, 0.0));
    mixes.push_back(make("chords", chords(kLen), 0.0, 0.25f, 0.0));
    mixes.push_back(make("bass", bass, 0.0, 0.5f, 0.0));
    mixes.push_back(make("vibrato", lead, 0.0, 0.4f, 35.0));
    mixes.push_back(make("detune-30ct", lead, -30.0, 0.4f, 0.0));
    mixes.push_back(make("detune+30ct", lead, 30.0, 0.4f, 0.0));
    Mix noisy = make("noise", lead, 0.0, 0.4f, 0.0);
    addNoise(noisy.audio, 0.05f);
    mixes.push_back(noisy);
    Mix drums = make("drums+melody", lead, 0.0, 0.35f, 0.0);
    addInto(drums.audio, drumTrack(kLen));
    mixes.push_back(drums);
    Mix full = make("full mix", lead, 0.0, 0.3f, 0.0);
    std::vector<Truth> all = lead;
    all.insert(all.end(), bass.begin(), bass.end());
    const std::vector<Truth> pads = chords(kLen);
    all.insert(all.end(), pads.begin(), pads.end());
    full.audio = renderTruth(all, 0.0, kLen, true, 0.22f, 0.0);
    full.truth = all;
    addInto(full.audio, drumTrack(kLen));
    addNoise(full.audio, 0.02f);
    mixes.push_back(full);
    return mixes;
}


}  // namespace oldmix
