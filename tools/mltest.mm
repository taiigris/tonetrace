// Neural engine with the real CoreML model: sawtooth bass F1 held plus F2 with its own attacks, speed, and load failures.
// Prints MLTEST PASSED / MLTEST FAILED (run by build.sh). Needs build/ml/nmp.mlmodelc (built by the tonetrace_model target).
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "TestSignals.h"
#include "analysis/Spectrogram.h"
#include "analysis/ml/NeuralEngine.h"
#include "analysis/ml/NeuralTranscriber.h"

using namespace ttr;

namespace {

constexpr double kRate = 44100.0;
constexpr double kMaxRealtimeFactor = 0.05;  // plan 4.5: 120 s of audio in at most 6 s
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

// Share of [a, b] covered by notes of `pitch`.
double coverage(const std::vector<NoteEvent>& notes, int pitch, double a, double b) {
    double covered = 0.0;
    for (const NoteEvent& n : notes)
        if (n.pitch == pitch) covered += std::max(0.0, std::min(b, n.endSec) - std::max(a, n.startSec));
    return covered / (b - a);
}

void octaveTest() {
    const std::vector<tsig::Tone> tones = {{29, 0.0, 6.0, 0.5f}, {41, 1.0, 2.5, 0.3f}, {41, 3.0, 4.5, 0.3f}, {41, 5.0, 5.8, 0.3f}};
    const std::vector<float> audio = tsig::synth(tones, kRate, 6.5, true);
    const std::shared_ptr<Spectrogram> spec = computeSpectrogram(audio.data(), audio.size(), kRate);
    const NeuralGrab grab = neuralGrabAudio(audio, kRate, spec.get(), 120.0, 0.0, 0.5f, FilterSettings{}, nullptr);
    check(grab.ok, "neural grab runs with the bundled model");
    for (const NoteEvent& n : grab.notes) std::printf("     note %d %.2f-%.2f\n", n.pitch, n.startSec, n.endSec);
    char line[160];
    const double f1 = coverage(grab.notes, 29, 0.2, 5.8);
    std::snprintf(line, sizeof line, "held F1 found (%.0f %% of the note)", f1 * 100.0);
    check(f1 >= 0.7, line);
    const double f2a = coverage(grab.notes, 41, 1.1, 2.4), f2b = coverage(grab.notes, 41, 3.1, 4.4), f2c = coverage(grab.notes, 41, 5.1, 5.7);
    std::snprintf(line, sizeof line, "F2 with its own attacks found (%.0f / %.0f / %.0f %%)", f2a * 100.0, f2b * 100.0, f2c * 100.0);
    check(f2a >= 0.5 && f2b >= 0.5 && f2c >= 0.5, line);
}

void speedTest() {
    const std::vector<tsig::Tone> tones = {{36, 0.0, 60.0, 0.4f}, {60, 0.0, 60.0, 0.2f}};
    const std::vector<float> audio = tsig::synth(tones, kRate, 60.0, true);
    const auto t0 = std::chrono::steady_clock::now();
    const NeuralGrab grab = neuralGrabAudio(audio, kRate, nullptr, 120.0, 0.0, 0.5f, FilterSettings{}, nullptr);
    const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    char line[160];
    std::snprintf(line, sizeof line, "60 s analysed in %.2f s (realtime factor %.3f, limit %.2f)", sec, sec / 60.0, kMaxRealtimeFactor);
    check(grab.ok && sec / 60.0 <= kMaxRealtimeFactor, line);
}

void silenceTest() {
    std::vector<float> audio(size_t(4.0 * kRate), 0.0f);
    const std::vector<float> tone = tsig::synth({{57, 0.5, 1.5, 0.4f}}, kRate, 1.5, true);
    std::copy(tone.begin(), tone.end(), audio.begin());
    const NeuralGrab grab = neuralGrabAudio(audio, kRate, nullptr, 120.0, 0.0, 1.0f, FilterSettings{}, nullptr);
    size_t inSilence = 0;
    for (const NoteEvent& n : grab.notes) inSilence += n.startSec > 1.6 ? 1 : 0;
    char line[160];
    std::snprintf(line, sizeof line, "digital silence gives no notes (%zu notes after the tone)", inSilence);
    check(grab.ok && inSilence == 0, line);
}

void loadFailureTest() {
    std::string reason;
    check(!loadCoreMLModel("/nonexistent/nmp.mlmodelc", &reason) && !reason.empty(), "a missing model is reported, not a crash");
}

}  // namespace

int main(int argc, char** argv) {
    if (!std::getenv(kNeuralModelEnv)) setenv(kNeuralModelEnv, "build/ml/nmp.mlmodelc", 0);
    installCoreMLLoader();
    std::string reason;
    if (!neuralModel(&reason)) {
        std::printf("FAIL model: %s\nMLTEST FAILED\n", reason.c_str());
        return 1;
    }
    loadFailureTest();
    octaveTest();
    silenceTest();
    speedTest();
    std::printf(failures == 0 ? "MLTEST PASSED\n" : "MLTEST FAILED\n");
    return failures == 0 ? 0 : 1;
}
