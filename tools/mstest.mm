// MuScriptor engine with the real model on real music: muscriptor.cpp's own 15 s test excerpt (a rock band, CC BY 4.0,
// fetched into the build tree, never committed). Checks that the plugin path (44.1 kHz int16 recording -> resampler -> model
// -> Tonetrace notes) gives the same tonal notes as calling the library directly, plus speed, silence and load failures.
// Prints MSTEST PASSED / MSTEST FAILED / MSTEST SKIPPED (no model on this machine or Intel; run by build.sh).
// Model: argv[1], else ~/Library/Tonetrace/models/muscriptor-medium-f16.gguf (downloaded by the plugin on first use).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"
#include "analysis/ml/MuScriptorEngine.h"
#include "analysis/ml/MuScriptorTranscriber.h"
#include "analysis/ml/Resampler.h"
#include "analysis/ml/muscriptor/MuScriptorBridge.h"

using namespace ttr;

namespace {

constexpr double kHostRate = 44100.0;
constexpr double kOnsetTolerance = 0.03;  // seconds
constexpr double kMinMatchShare = 0.9;     // plugin path vs direct call
constexpr double kMaxRealtimeFactor = 1.0;
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

// 16 kHz mono float32 WAV (the fixture's format). Empty on any mismatch.
std::vector<float> readFixture(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    const std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0) return {};
    for (size_t pos = 12; pos + 8 <= b.size();) {
        uint32_t size = 0;
        std::memcpy(&size, b.data() + pos + 4, 4);
        if (std::memcmp(b.data() + pos, "data", 4) == 0) {
            std::vector<float> out(std::min<size_t>(size, b.size() - pos - 8) / sizeof(float));
            std::memcpy(out.data(), b.data() + pos + 8, out.size() * sizeof(float));
            return out;
        }
        pos += 8 + size + (size & 1u);
    }
    return {};
}

Snapshot snapshotOf(const std::vector<float>& audio, double rate) {
    Snapshot snap;
    snap.spectrogram = computeSpectrogram(audio.data(), audio.size(), rate);
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = rate;
    for (float v : audio) clip->samples.push_back(int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)));
    snap.audio = clip;
    snap.bpm = 120.0;
    return snap;
}

void fixtureTest(const std::string& model, const std::string& fixture) {
    const std::vector<float> audio16k = readFixture(fixture);
    check(audio16k.size() > size_t(10.0 * kMuScriptorRate), "fixture audio read");
    if (audio16k.empty()) return;
    // Reference: the library directly on the original 16 kHz signal.
    std::string why;
    std::shared_ptr<msb::Model> direct = msb::load(model, &why);
    std::vector<msb::Note> reference;
    check(direct && msb::transcribe(*direct, audio16k.data(), audio16k.size(), &reference, {}, {}, &why), "direct library call runs");
    direct.reset();
    // Plugin path: a 44.1 kHz int16 recording, as the processor stores it.
    const std::vector<float> host = resampleMono(audio16k.data(), audio16k.size(), kMuScriptorRate, kHostRate);
    const Snapshot snap = snapshotOf(host, kHostRate);
    const auto t0 = std::chrono::steady_clock::now();
    const GrabOutcome out = quickGrabWithEngine(snap, 0.5f, FilterSettings{}, 0.0, GrabEngine::MuScriptor);
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const double length = double(audio16k.size()) / kMuScriptorRate;
    if (!out.fallbackReason.empty()) std::printf("     fallback: %s\n", out.fallbackReason.c_str());
    std::printf("     %.1f s of music in %.1f s (RTF %.2f, incl. model load), %zu notes; direct call %zu notes\n", length, secs, secs / length,
                out.notes.size(), reference.size());
    check(out.used == GrabEngine::MuScriptor, "MuScriptor grab runs with the real model");
    int tonal = 0, matched = 0;
    for (const msb::Note& r : reference) {
        if (r.isDrum || r.pitch < kMinNote || r.pitch > kMaxNote) continue;
        ++tonal;
        for (const NoteEvent& n : out.notes)
            if (n.pitch == r.pitch && std::abs(n.startSec - r.onset) < kOnsetTolerance) { ++matched; break; }
    }
    const double share = tonal > 0 ? double(matched) / tonal : 0.0;
    std::printf("     %d of %d tonal reference notes found through the plugin path (%.0f %%)\n", matched, tonal, share * 100.0);
    check(tonal >= 20 && share >= kMinMatchShare, "plugin path matches the direct call (>= 90 % of tonal notes)");
    check(secs / length < kMaxRealtimeFactor, "faster than real time");
}

void silenceTest() {
    const Snapshot snap = snapshotOf(std::vector<float>(size_t(3.0 * kHostRate), 0.0f), kHostRate);
    const GrabOutcome out = quickGrabWithEngine(snap, 0.5f, FilterSettings{}, 0.0, GrabEngine::MuScriptor);
    check(out.used == GrabEngine::MuScriptor && out.notes.empty(), "silence gives no notes");
}

}  // namespace

int main(int argc, char** argv) {
    if (!msb::isSupported()) { std::printf("MSTEST SKIPPED (MuScriptor needs Apple Silicon)\n"); return 0; }
    setenv(kMuScriptorModelEnv, "/nonexistent/muscriptor.gguf", 1);
    installMuScriptorLoader();
    std::string why;
    check(!noteTranscriber(&why) && why.find("model not found") != std::string::npos, "missing model file gives a reason");
    unsetenv(kMuScriptorModelEnv);
    const std::string model = argc > 1 && *argv[1] ? argv[1] : muscriptorModelPath();
    if (FILE* f = std::fopen(model.c_str(), "rb")) {
        std::fclose(f);
    } else {
        std::printf("MSTEST SKIPPED (no model at %s)\n", model.c_str());
        return failures == 0 ? 0 : 1;
    }
    setenv(kMuScriptorModelEnv, model.c_str(), 1);
    fixtureTest(model, argc > 2 ? argv[2] : TONETRACE_MS_FIXTURE);
    silenceTest();
    std::printf("%s\n", failures == 0 ? "MSTEST PASSED" : "MSTEST FAILED");
    return failures == 0 ? 0 : 1;
}
