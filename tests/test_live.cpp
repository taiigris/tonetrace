#include <cmath>

#include "TestDrums.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/LiveSpectrum.h"
#include "analysis/Spectrogram.h"
#include "analysis/WavePeaks.h"

using namespace ttr;

TEST(live_spectrogram_grows_and_matches_the_final_analysis) {
    constexpr double kRate = 44100.0;
    const auto sig = tsig::synth(tsig::chordThenMelody(), kRate, 6.5, true);
    LiveSpectrum live;
    live.prepare(kRate, 10.0);
    int lastFrames = 0;
    for (size_t available = 4096; available <= sig.size(); available += 4410) {  // 100 ms chunks of arriving audio
        live.advance(sig.data(), available);
        CHECK(live.frames() >= lastFrames);  // never shrinks
        lastFrames = live.frames();
    }
    live.advance(sig.data(), sig.size());
    const auto full = computeSpectrogram(sig.data(), sig.size(), kRate);
    CHECK(full != nullptr);
    CHECK(live.frames() > full->frames - 12 && live.frames() <= full->frames);  // only the last window is missing
    CHECK(std::fabs(live.hopSec() - full->hopSec) < 1e-12);
    float worst = 0.0f;
    for (int f = 0; f < live.frames(); ++f)
        for (int n = 0; n < kNumNotes; ++n)
            worst = std::max(worst, std::fabs(live.raw()[size_t(f) * kNumNotes + size_t(n)] - full->rawAt(f, n)));
    CHECK_MSG(worst < 1e-3f * full->peak, "live and final frames differ by %g (peak %g)", double(worst), double(full->peak));
    CHECK(live.peak() > 0.0f && live.peak() <= full->peak * 1.0001f);

    live.reset();
    CHECK(live.frames() == 0 && live.peak() == 0.0f);
}

TEST(peak_builder_incremental_equals_one_shot_and_follows_the_signal) {
    constexpr double kRate = 44100.0;
    const auto sig = tsig::synth({{57, 0.0, 1.0, 0.5f}, {64, 1.0, 2.0, 0.25f}}, kRate, 2.0, false);
    PeakBuilder incremental, oneShot;
    incremental.prepare(kRate, 5.0);
    oneShot.prepare(kRate, 5.0);
    for (size_t available = 1000; available < sig.size(); available += 1000) incremental.advance(sig.data(), available);
    incremental.advance(sig.data(), sig.size());
    oneShot.advance(sig.data(), sig.size());
    const auto a = incremental.finish(), b = oneShot.finish();
    CHECK(a->minMax == b->minMax);
    CHECK(std::abs(a->count() - 2 * kPeaksPerSecond) <= 2);
    float firstHalf = 0, secondHalf = 0;
    for (int i = 20; i < 180; ++i) firstHalf = std::max(firstHalf, a->minMax[size_t(i) * 2 + 1]);
    for (int i = 220; i < 380; ++i) secondHalf = std::max(secondHalf, a->minMax[size_t(i) * 2 + 1]);
    CHECK(firstHalf > 0.45f && firstHalf <= 0.51f);
    CHECK(secondHalf > 0.2f && secondHalf <= 0.26f);
    for (int i = 0; i < a->count(); ++i) CHECK(a->minMax[size_t(i) * 2] <= a->minMax[size_t(i) * 2 + 1]);
    incremental.reset();
    CHECK(incremental.count() == 0);
}

TEST(live_harmonic_part_follows_the_final_hpss_with_a_small_delay) {
    constexpr double kRate = 44100.0, kLength = 6.0;
    auto sig = tsig::synth({{57, 0.0, 3.0, 0.4f}, {64, 1.0, 5.0, 0.3f}, {60, 3.0, 6.0, 0.3f}}, kRate, kLength, false);
    const auto drums = tdrum::render(tdrum::pattern(120.0, kLength), kRate, kLength, 0.8f);
    for (size_t i = 0; i < sig.size(); ++i) sig[i] += drums[i];

    LiveSpectrum live;
    live.prepare(kRate, 10.0);
    for (size_t available = 4096; available <= sig.size(); available += 2205) {  // 50 ms chunks
        live.advance(sig.data(), available);
        CHECK(live.harmonicFrames() <= live.frames());
        CHECK(live.harmonicFrames() >= live.frames() - 6);  // finished a few frames behind the raw spectrum
    }
    live.advance(sig.data(), sig.size());
    const auto full = computeSpectrogram(sig.data(), sig.size(), kRate);
    CHECK(full && live.harmonicFrames() > 200);

    // the live harmonic part is close to the offline one (it only knows the past, so the mask epsilon differs a little)
    double diff = 0.0, energy = 0.0, rawEnergy = 0.0, liveEnergy = 0.0;
    for (int f = 8; f < live.harmonicFrames(); ++f)
        for (int n = 0; n < kNumNotes; ++n) {
            const float l = live.harmonic()[size_t(f) * kNumNotes + size_t(n)], h = full->harmAt(f, n);
            diff += std::fabs(l - h);
            energy += h;
            liveEnergy += l;
            rawEnergy += full->rawAt(f, n);
        }
    CHECK_MSG(diff < 0.15 * energy, "live harmonic part differs from the final one by %.1f%%", 100.0 * diff / energy);
    CHECK_MSG(liveEnergy < 0.9 * rawEnergy, "Hide drums must remove energy while recording (%.2f of raw)", liveEnergy / rawEnergy);

    live.reset();
    CHECK(live.harmonicFrames() == 0);
}
