// Pitch lines (peak map): tuning, peak accuracy, semitone separation, overtone dimming, vibrato, click-to-grab, onset snap.
#include <algorithm>
#include <cmath>

#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/LineBuilder.h"
#include "analysis/PitchLines.h"
#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"
#include "analysis/SpectralFrames.h"
#include "core/NoteEdit.h"

using namespace ttr;

namespace {

constexpr double kRate = 44100.0;

struct Analysed {
    std::shared_ptr<Spectrogram> spec;
    std::shared_ptr<PitchLines> lines;
};

Analysed analyse(const std::vector<float>& signal, double sr = kRate) {
    Analysed a;
    a.spec = computeSpectrogram(signal.data(), signal.size(), sr);
    const DecimatedSignal d = decimate(signal.data(), signal.size(), sr);
    a.lines = computePitchLines(d.samples.data(), long(d.samples.size()), d.sampleRate, a.spec.get());
    return a;
}

double hzToMidi(double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); }
double centsBetween(double a, double b) { return (a - b) * 100.0; }

// The strongest visible point of the frame at `sec`.
LinePoint strongestAt(const PitchLines& L, double sec) {
    const int f = std::clamp(int(std::lround(sec / L.hopSec)), 0, L.frames - 1);
    LinePoint best;
    for (int i = 0; i < L.counts[size_t(f)]; ++i)
        if (!(L.frame(f)[i].flags & kLineHidden) && L.frame(f)[i].level > best.level) best = L.frame(f)[i];
    return best;
}

}  // namespace

TEST(lines_tuning_detected_for_sine_and_saw_at_plus_30_cents) {
    const double a3 = 220.0 * std::pow(2.0, 0.30 / 12.0), e4 = 329.6276 * std::pow(2.0, 0.30 / 12.0), c4 = 261.6256 * std::pow(2.0, 0.30 / 12.0);
    for (const bool saw : {false, true}) {
        const auto sig = tsig::synthHz({{a3, 0.0, 2.0, 0.4f, saw}, {c4 * 1.0, 0.0, 2.0, 0.3f, saw}, {e4, 0.0, 2.0, 0.3f, saw}}, kRate, 2.0);
        const Analysed a = analyse(sig);
        CHECK(a.lines != nullptr);
        if (!a.lines) return;
        const double tuning = a.lines->tuningCents;
        std::printf("    %s +30 ct: tuning %+.2f ct (confidence %.2f)\n", saw ? "saw " : "sine", tuning, double(a.lines->tuningConfidence));
        CHECK_MSG(std::fabs(tuning - 30.0) <= 3.0, "tuning %.2f", tuning);
        const LineHit hit = a.lines->hitAt(1.0, hzToMidi(a3), 0.5);
        CHECK(hit.track >= 0);
        CHECK(a.lines->noteForTrack(hit.track, tuning).pitch == 57);                       // A3
        CHECK(a.lines->noteForTrack(a.lines->hitAt(1.0, hzToMidi(e4), 0.5).track, tuning).pitch == 64);  // E4
        CHECK(a.lines->noteForTrack(a.lines->hitAt(1.0, hzToMidi(c4), 0.5).track, tuning).pitch == 60);  // C4
    }
}

TEST(lines_peak_frequency_within_5_cent_for_20_random_sines) {
    uint32_t state = 12345;
    auto uniform = [&] { state = state * 1664525u + 1013904223u; return double(state >> 8) / double(1u << 24); };
    double worst = 0.0;
    for (int i = 0; i < 20; ++i) {
        const double hz = 80.0 * std::pow(4000.0 / 80.0, uniform());
        const Analysed a = analyse(tsig::synthHz({{hz, 0.0, 1.5, 0.5f}}, kRate, 1.5));
        CHECK(a.lines != nullptr);
        if (!a.lines) continue;
        const LinePoint p = strongestAt(*a.lines, 0.75);
        const double error = std::fabs(centsBetween(p.midi, hzToMidi(hz)));
        worst = std::max(worst, error);
        CHECK_MSG(error <= 5.0, "%.2f Hz: %.2f ct", hz, error);
    }
    std::printf("    worst peak error over 20 random sines (80 Hz - 4 kHz): %.2f ct\n", worst);
}

TEST(lines_two_tones_a_semitone_apart_stay_two_lines) {
    const struct { int low; const char* name; } cases[] = {{40, "E2+F2"}, {72, "C5+C#5"}};
    for (const auto& c : cases) {
        const auto sig = tsig::synth({{c.low, 0.0, 2.0, 0.4f}, {c.low + 1, 0.0, 2.0, 0.4f}}, kRate, 2.0, false);
        const Analysed a = analyse(sig);
        CHECK(a.lines != nullptr);
        if (!a.lines) return;
        const LineHit lo = a.lines->hitAt(1.0, c.low, 0.3), hi = a.lines->hitAt(1.0, c.low + 1, 0.3);
        CHECK(lo.track >= 0 && hi.track >= 0 && lo.track != hi.track);
        if (lo.track >= 0 && hi.track >= 0)
            std::printf("    %s: lines at %+.1f ct and %+.1f ct from their rows\n", c.name, centsBetween(lo.midi, c.low), centsBetween(hi.midi, c.low + 1));
        CHECK(a.lines->noteForTrack(lo.track, 0).pitch == c.low);
        CHECK(a.lines->noteForTrack(hi.track, 0).pitch == c.low + 1);
    }
}

TEST(lines_overtones_of_a_saw_are_dimmed_and_the_fundamental_stays_full) {
    const auto sig = tsig::synth({{45, 0.0, 2.5, 0.6f}}, kRate, 2.5, true);  // A2
    const Analysed a = analyse(sig);
    CHECK(a.lines != nullptr);
    if (!a.lines) return;
    const PitchLines& L = *a.lines;
    const float gain = 0.5f, range = 0.5f;
    const LinePoint fundamental = strongestAt(L, 1.2);
    const float full = lineBrightness(fundamental, L.peak, gain, range, true);
    CHECK_MSG(full >= 0.95f, "fundamental brightness %.2f", double(full));
    CHECK(!(fundamental.flags & kLineHarmonic));
    int dimmed = 0;
    for (const int harmonic : {2, 3, 4, 5, 6}) {
        const double midi = 45.0 + 12.0 * std::log2(double(harmonic));
        const LineHit hit = L.hitAt(1.2, midi, 0.35);
        CHECK_MSG(hit.track >= 0, "harmonic %d not found", harmonic);
        if (hit.track < 0) continue;
        LinePoint p;
        for (int i = 0; i < L.counts[size_t(hit.frame)]; ++i) if (L.frame(hit.frame)[i].track == hit.track) p = L.frame(hit.frame)[i];
        const float b = lineBrightness(p, L.peak, gain, range, true);
        std::printf("    harmonic %d: brightness %.3f (%.0f %% of the fundamental)\n", harmonic, double(b), double(b / full) * 100.0);
        CHECK_MSG(b <= 0.25f * full, "harmonic %d at %.2f", harmonic, double(b / full));
        dimmed += b <= 0.25f * full;
    }
    CHECK(dimmed == 5);
    const LinePoint raw = L.frame(int(1.2 / L.hopSec))[0];
    CHECK(lineBrightness(raw, L.peak, gain, range, false) >= lineBrightness(raw, L.peak, gain, range, true));
}

TEST(lines_vibrato_of_40_cent_at_5_hz_is_one_line_on_the_right_row) {
    const auto sig = tsig::synthHz({{440.0, 0.5, 2.5, 0.5f, false, 40.0, 5.0}}, kRate, 3.0);
    const Analysed a = analyse(sig);
    CHECK(a.lines != nullptr);
    if (!a.lines) return;
    int visibleLong = 0, visible = 0;
    for (const LineTrack& t : a.lines->tracks) {
        if (t.flags & kLineHidden) continue;
        ++visible;
        if (t.endSec - t.startSec > 1.7) ++visibleLong;
    }
    std::printf("    vibrato: %d visible track(s), %d long\n", visible, visibleLong);
    CHECK(visibleLong == 1);
    CHECK(visible == 1);
    const LineHit hit = a.lines->hitAt(1.5, 69.0, 0.6);
    CHECK(hit.track >= 0);
    const NoteEvent n = a.lines->noteForTrack(hit.track, a.lines->tuningCents);
    CHECK(n.pitch == 69);
    CHECK_MSG(std::fabs(n.startSec - 0.5) <= 0.03 && std::fabs(n.endSec - 2.5) <= 0.03, "%.3f-%.3f", n.startSec, n.endSec);
}

TEST(lines_click_to_grab_edges_within_30_ms) {
    for (const int pitch : {45, 57, 69, 81, 93}) {
        const auto sig = tsig::synth({{pitch, 1.00, 1.75, 0.5f}, {pitch + 7, 0.3, 0.8, 0.2f}}, kRate, 2.5, true);
        const Analysed a = analyse(sig);
        CHECK(a.lines != nullptr);
        if (!a.lines) return;
        const LineHit hit = a.lines->hitAt(1.4, pitch, 0.5);
        CHECK_MSG(hit.track >= 0, "pitch %d: no line under the cursor", pitch);
        if (hit.track < 0) continue;
        const NoteEvent n = a.lines->noteForTrack(hit.track, a.lines->tuningCents);
        std::printf("    pitch %d: start %.3f s (%+.0f ms), end %.3f s (%+.0f ms)\n", pitch, n.startSec, (n.startSec - 1.0) * 1000.0, n.endSec, (n.endSec - 1.75) * 1000.0);
        CHECK(n.pitch == pitch);
        CHECK_MSG(std::fabs(n.startSec - 1.00) <= 0.03, "pitch %d start %.3f", pitch, n.startSec);
        CHECK_MSG(std::fabs(n.endSec - 1.75) <= 0.03, "pitch %d end %.3f", pitch, n.endSec);
    }
}

TEST(lines_onset_snap_hits_note_starts_within_15_ms) {
    const double starts[] = {0.50, 1.13, 1.80, 2.41};
    std::vector<tsig::Tone> tones;
    for (size_t i = 0; i < 4; ++i) tones.push_back({60 + int(i) * 2, starts[i], starts[i] + 0.45, 0.5f});
    const Analysed a = analyse(tsig::synth(tones, kRate, 3.2, true));
    CHECK(a.lines != nullptr);
    if (!a.lines) return;
    TimeGrid grid;
    grid.snapMode = kSnapOnset;
    grid.lines = a.lines;
    double worst = 0.0;
    for (const double start : starts) {
        const double snapped = snapTime(start + 0.030, grid), error = std::fabs(snapped - start);
        worst = std::max(worst, error);
        CHECK_MSG(error <= 0.015, "start %.2f snapped to %.4f", start, snapped);
        const NoteEvent n = makeNote(60, start + 0.025, start + 0.43, grid, true);
        CHECK_MSG(std::fabs(n.startSec - start) <= 0.015, "makeNote start %.4f", n.startSec);
        CHECK_MSG(std::fabs(n.endSec - (start + 0.45)) <= 0.04, "makeNote end %.4f", n.endSec);
    }
    std::printf("    worst onset snap error: %.1f ms\n", worst * 1000.0);
    grid.snapMode = 0;
    CHECK(snapTime(0.527, grid) == 0.527);
}

TEST(lines_repeated_note_of_the_same_pitch_is_split_at_the_onset) {
    const Analysed a = analyse(tsig::synth({{64, 0.5, 1.2, 0.5f}, {64, 1.25, 2.0, 0.5f}}, kRate, 2.5, true));
    CHECK(a.lines != nullptr);
    if (!a.lines) return;
    const LineHit first = a.lines->hitAt(0.9, 64, 0.4), second = a.lines->hitAt(1.7, 64, 0.4);
    CHECK(first.track >= 0 && second.track >= 0 && first.track != second.track);
}

TEST(lines_quick_grab_finds_fundamentals_without_overtones) {
    const Analysed a = analyse(tsig::synth({{57, 0.0, 1.0, 0.5f}, {64, 1.0, 2.0, 0.5f}}, kRate, 2.2, true));
    CHECK(a.lines != nullptr);
    if (!a.lines) return;
    const std::vector<NoteEvent> notes = quickGrabFromLines(*a.lines, 0.5f, a.lines->tuningCents, FilterSettings{});
    CHECK(notes.size() == 2);
    if (notes.size() == 2) CHECK(notes[0].pitch == 57 && notes[1].pitch == 64);
}

TEST(lines_magnet_pulls_to_the_row_of_the_line) {
    const Analysed a = analyse(tsig::synth({{69, 1.0, 3.0, 0.5f}}, kRate, 4.0, false));
    CHECK(a.lines != nullptr);
    if (!a.lines) return;
    CHECK(a.lines->magnetPitch(1.5, 2.0, 70, 0.0) == 69);
    CHECK(a.lines->magnetPitch(1.5, 2.0, 68, 0.0) == 69);
    CHECK(a.lines->magnetPitch(1.5, 2.0, 74, 0.0) == 74);
    CHECK(a.lines->magnetPitch(0.1, 0.5, 70, 0.0) == 70);
}
