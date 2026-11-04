// A synthetic 32 s band song for the UI tools: guitar riff, bass, a sung line, a synth pad and a drum kit, as the notes
// the model "finds" (fed chunk by chunk by tk::FakeTranscriber) and as audio (stereo, so the waveform looks real).
#pragma once
#include <cmath>
#include <vector>

#include "TestKernel.h"

namespace demo {

constexpr double kSeconds = 32.0;
constexpr double kBeat = 0.5;  // 120 bpm
constexpr double kChunkSec = 5.0;

inline void add(std::vector<ttr::TranscribedNote>& out, double start, double length, int pitch, int program) {
    out.push_back({start, start + length, pitch, program == ttr::kDrumProgram, program});
}

// Every note of the song (onsets in seconds).
inline std::vector<ttr::TranscribedNote> notes() {
    std::vector<ttr::TranscribedNote> n;
    const int bassLine[8] = {40, 40, 43, 45, 36, 36, 38, 43};  // E E G A C C D G, one bar each
    const int riff[8] = {64, 67, 69, 67, 64, 62, 64, 67};
    for (int bar = 0; bar < 16; ++bar) {
        const double t = bar * 4 * kBeat;
        const int root = bassLine[bar % 8];
        add(n, t, 1.4, root, 33);
        add(n, t + 1.5, 0.45, root + 7, 33);
        for (int i = 0; i < 8; ++i) add(n, t + i * 0.25, 0.22, riff[(i + bar) % 8] - (bar % 4 == 3 ? 2 : 0), 26);
        add(n, t, 2.0, root + 24, 88);  // pad chord
        add(n, t, 2.0, root + 28, 88);
        add(n, t, 2.0, root + 31, 88);
        for (int beat = 0; beat < 4; ++beat) {
            const double b = t + beat * kBeat;
            add(n, b, 0.01, beat % 2 == 0 ? 36 : 38, ttr::kDrumProgram);
            add(n, b, 0.01, 42, ttr::kDrumProgram);
            add(n, b + 0.25, 0.01, 42, ttr::kDrumProgram);
        }
        if (bar % 4 == 0) add(n, t, 0.01, 49, ttr::kDrumProgram);
        if (bar >= 4 && bar % 2 == 0) {  // the voice enters in bar 5
            const int melody[6] = {71, 72, 74, 72, 71, 69};
            for (int i = 0; i < 6; ++i) add(n, t + 0.3 + i * 0.5, i == 5 ? 1.2 : 0.42, melody[i], 52);
        }
    }
    return n;
}

// The notes in model chunks of 5 s (released by their offset, like the real model).
inline std::vector<std::vector<ttr::TranscribedNote>> chunks() {
    const int count = int(std::ceil(kSeconds / kChunkSec));
    std::vector<std::vector<ttr::TranscribedNote>> out(static_cast<size_t>(count));
    for (const ttr::TranscribedNote& t : notes()) out[size_t(std::min(count - 1, int(t.offset / kChunkSec)))].push_back(t);
    return out;
}

// The song as audio: soft tones for the notes, noise bursts for the drums.
inline std::vector<float> audio(double rate) {
    std::vector<float> out(size_t(kSeconds * rate), 0.0f);
    uint32_t seed = 12345;
    for (const ttr::TranscribedNote& t : notes()) {
        const size_t a = size_t(t.onset * rate), len = size_t((t.isDrum ? 0.12 : t.offset - t.onset) * rate);
        const double hz = 440.0 * std::pow(2.0, (t.pitch - 69) / 12.0);
        const float amp = t.isDrum ? (t.pitch == 36 ? 0.35f : 0.18f) : (t.program == 88 ? 0.04f : 0.09f);
        for (size_t i = 0; i < len && a + i < out.size(); ++i) {
            const double env = std::exp(-double(i) / (rate * (t.isDrum ? 0.05 : 0.4)));
            float s;
            if (t.isDrum) {
                seed = seed * 1664525u + 1013904223u;
                s = t.pitch == 36 ? float(std::sin(2.0 * M_PI * 55.0 * double(i) / rate)) : float(int32_t(seed)) / 2147483648.0f;
            } else {
                s = float(std::sin(2.0 * M_PI * hz * double(i) / rate));
            }
            out[a + i] += amp * float(env) * s;
        }
    }
    return out;
}

// A decoder that "reads" the demo song for any path.
inline void installDecoder() {
    ttr::setAudioDecoder([](const std::string&, double rate, double, ttr::DecodedAudio* out, std::string*) {
        const std::vector<float> mono = audio(rate);
        out->samples.resize(mono.size() * 2);
        for (size_t i = 0; i < mono.size(); ++i) {
            out->samples[2 * i] = tk::toInt16(mono[i]);
            out->samples[2 * i + 1] = tk::toInt16(mono[i] * 0.9f);
        }
        out->channels = 2;
        out->sampleRate = rate;
        out->fileSeconds = kSeconds;
        return true;
    });
}

}  // namespace demo
