// 4.3: defaults, free notes (draw / move anywhere, magnet never moves), 4.2 states, the full spectrum data, Quick Grab >= 120 ms.
#include <algorithm>
#include <cmath>

#include "TestKernel.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/LineBuilder.h"
#include "analysis/LineFrames.h"
#include "analysis/LiveLines.h"
#include "analysis/QuickGrab.h"
#include "analysis/SpectralFrames.h"
#include "analysis/Spectrogram.h"
#include "core/NoteEdit.h"
#include "core/TonetraceKernel.h"
#include "state/Crc32.h"
#include "state/StateCodec.h"

using namespace ttr;

namespace {

constexpr double kRate = 44100.0;

TimeGrid grid16() {
    TimeGrid g;
    g.bpm = 120.0;
    g.snapMode = kSnapGrid16;
    return g;
}

// byte for byte what Tonetrace 4.2 wrote (editor state version 3), with Onset snap and Magnet on
std::vector<uint8_t> editorStateV3() {
    std::vector<uint8_t> body;
    auto put = [&](const auto& v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); body.insert(body.end(), p, p + sizeof(v)); };
    put(0.5f); put(0.5f); put(1.0f); put(0.0f);
    put(int32_t(5));                                    // snap Onset
    put(uint8_t(1)); put(uint8_t(1)); put(uint8_t(0));  // hide drums on, magnet on, sync off
    put(0.0); put(-1.0);
    put(int32_t(1100)); put(int32_t(700));
    put(uint8_t(1)); put(uint8_t(1)); put(0.0f);        // fundamentals, align, trim
    put(uint8_t(0));                                    // waveform lane hidden
    std::vector<uint8_t> out;
    auto putOut = [&](uint32_t v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); out.insert(out.end(), p, p + 4); };
    putOut(0x31555454u); putOut(3u); putOut(uint32_t(body.size())); putOut(crc32(body.data(), body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}  // namespace

TEST(new_instances_have_the_4_3_defaults) {
    CHECK(paramSpec(kParamSnap).defaultValue == 3.0f);  // 1/16
    CHECK(std::string(snapName(int(paramSpec(kParamSnap).defaultValue))) == "1/16");
    CHECK(paramSpec(kParamMagnet).defaultValue == 0.0f);
    CHECK(paramSpec(kParamHideDrums).defaultValue == 0.0f);
    const UiSettings ui;
    CHECK(ui.snap == 3 && !ui.magnet && !ui.hideDrums && !ui.fundamentals);
    CHECK(ui.isSpectrum && !ui.grabOnClick && !ui.samplerOnDrag);
    TonetraceKernel k;
    CHECK(k.param(kParamSnap) == 3.0f && k.param(kParamMagnet) == 0.0f && k.param(kParamHideDrums) == 0.0f);
    const LineView v = k.lineView();
    CHECK(v.isSpectrum && !v.grabOnClick && !v.samplerOnDrag && !v.fundamentals);
    CHECK(k.timeGrid().snapMode == 3);
}

TEST(a_4_2_editor_state_loads_with_onset_and_magnet_and_gets_the_new_fields) {
    const std::vector<uint8_t> blob = editorStateV3();
    UiSettings s;
    std::string reason;
    CHECK_MSG(decodeUiSettings(blob.data(), blob.size(), &s, &reason), "%s", reason.c_str());
    CHECK(s.snap == kSnapOnset && s.magnet && s.hideDrums && s.fundamentals && !s.showWave);
    CHECK(s.isSpectrum && !s.grabOnClick && !s.samplerOnDrag);  // the 4.2 user gets the Spectrum view
    const std::vector<uint8_t> again = encodeUiSettings(s);
    UiSettings back;
    CHECK(decodeUiSettings(again.data(), again.size(), &back) && back == s);
    s.isSpectrum = false;
    s.grabOnClick = true;
    s.samplerOnDrag = true;
    const std::vector<uint8_t> v4 = encodeUiSettings(s);
    CHECK(decodeUiSettings(v4.data(), v4.size(), &back) && back == s && !back.isSpectrum && back.grabOnClick && back.samplerOnDrag);
}

TEST(a_4_2_processor_state_keeps_onset_and_magnet) {
    ParamValues params = defaultParams();
    params[kParamSnap] = float(kSnapOnset);
    params[kParamMagnet] = 1.0f;
    params[kParamHideDrums] = 1.0f;
    const std::vector<uint8_t> blob = encodeState(params, nullptr, false);
    TonetraceKernel k;
    k.prepare(kRate, tk::kBlock);
    CHECK(k.loadState(blob.data(), blob.size()));
    CHECK(k.param(kParamSnap) == float(kSnapOnset) && k.param(kParamMagnet) == 1.0f && k.param(kParamHideDrums) == 1.0f);
    k.deactivate();
}

TEST(a_note_is_drawn_exactly_on_a_row_without_energy) {
    const TimeGrid g = grid16();
    const NoteEvent n = makeNote(31, 3.0, 3.5, g, true);  // G1: nothing is recorded there
    const std::vector<NoteEvent> notes = withNoteAdded({}, n);
    CHECK(notes.size() == 1 && notes[0].pitch == 31);
    CHECK(std::fabs(notes[0].startSec - 3.0) < 1e-9 && std::fabs(notes[0].endSec - 3.5) < 1e-9);
}

TEST(moving_a_note_never_follows_lines_or_onsets) {
    // a recording with one strong line and onsets: moving to the empty row of another pitch / time stays exactly there
    TonetraceKernel k;
    k.prepare(kRate, tk::kBlock);
    CHECK(tk::record(k, tsig::synth({{69, 0.5, 2.0, 0.5f}}, kRate, 2.5, true)));
    k.setParam(kParamMagnet, 1.0f);
    k.setParam(kParamSnap, float(kSnapOnset));
    const TimeGrid g = k.timeGrid();
    CHECK(g.lines != nullptr);
    const std::vector<NoteEvent> start = {makeNote(60, 0.0, 0.5, g, false)};
    const double target = g.lines->edges.empty() ? 0.51 : double(g.lines->edges.front()) + 0.02;  // close to an onset, but not on it
    CHECK(snapTimeToGrid(target, g) == target);  // Onset mode never pulls a move to an edge
    const std::vector<NoteEvent> moved = withMoved(start, {0}, target, 9);  // up to the row of the line (69) ...
    CHECK(moved[0].pitch == 69 && std::fabs(moved[0].startSec - target) < 1e-9);
    const std::vector<NoteEvent> elsewhere = withMoved(start, {0}, target, 12);  // ... or one above it: no line there
    CHECK(elsewhere[0].pitch == 72 && std::fabs(elsewhere[0].startSec - target) < 1e-9 && std::fabs(elsewhere[0].endSec - (target + 0.5)) < 1e-9);
    TimeGrid grid = g;
    grid.snapMode = kSnapGrid16;
    const double snapped = snapTimeToGrid(0.4321, grid);
    CHECK(std::fabs(snapped - snapTime(0.4321, grid)) < 1e-12);
    k.deactivate();
}

TEST(alt_flips_snap_and_magnet_for_one_gesture) {
    TimeGrid g = grid16();
    CHECK(snapPlanFor(g, false).snap && snapPlanFor(g, false).grid.snapMode == kSnapGrid16);
    CHECK(!snapPlanFor(g, true).snap);                           // snap on -> free
    g.snapMode = 0;                                              // Off
    CHECK(!snapPlanFor(g, false).snap);
    CHECK(snapPlanFor(g, true).snap && snapPlanFor(g, true).grid.snapMode == kSnapGrid16);  // Off -> grid
    CHECK(!magnetActive(false, false) && magnetActive(true, false) && magnetActive(false, true) && !magnetActive(true, true));
}

TEST(the_fine_spectrum_has_8_bins_per_semitone_and_peaks_at_the_tone) {
    CHECK(kFineBins == kNumNotes * 8);
    const std::vector<float> sig = tsig::synth({{69, 0.0, 1.0, 0.5f, true}}, kRate, 1.0, false);
    const DecimatedSignal d = decimate(sig.data(), sig.size(), kRate);
    const std::shared_ptr<PitchLines> lines = computePitchLines(d.samples.data(), long(d.samples.size()), d.sampleRate, nullptr);
    CHECK(lines && lines->fine.size() == size_t(lines->frames) * kFineBins);
    if (!lines) return;
    const uint8_t* row = &lines->fine[size_t(lines->frames / 2) * kFineBins];
    const int expected = int(std::lround((69 - (kMinNote - 0.5)) * kFineBinsPerNote - 0.5));  // A4 sits between the bins 3 and 4 of its row
    int best = 0;
    for (int j = 0; j < kFineBins; ++j) if (row[j] > row[best]) best = j;
    CHECK_MSG(std::abs(best - expected) <= 1, "peak bin %d, expected %d", best, expected);
    const float peak = fineCodeToAmplitude(row[best]);
    CHECK_MSG(std::fabs(peak - 0.5f) < 0.06f, "peak amplitude %.3f (a 0.5 sine)", double(peak));
    CHECK(row[best] > row[std::min(kFineBins - 1, best + 24)] + 40);  // three semitones away it is far below
    CHECK(row[0] < row[best] - 100);
}

TEST(the_fine_spectrum_separates_neighbouring_semitones_and_is_empty_for_silence) {
    std::vector<float> sig = tsig::synth({{69, 0.0, 1.0, 0.4f, true}, {70, 0.0, 1.0, 0.4f, true}}, kRate, 1.2, false);
    const DecimatedSignal d = decimate(sig.data(), sig.size(), kRate);
    const std::shared_ptr<PitchLines> lines = computePitchLines(d.samples.data(), long(d.samples.size()), d.sampleRate, nullptr);
    CHECK(lines != nullptr);
    if (!lines) return;
    const uint8_t* row = &lines->fine[size_t(lines->frames / 3) * kFineBins];
    const int a = int(std::lround((69 - (kMinNote - 0.5)) * kFineBinsPerNote - 0.5)), b = a + kFineBinsPerNote;
    int valley = row[a];
    for (int j = a; j <= b; ++j) valley = std::min<int>(valley, row[j]);
    CHECK_MSG(valley < std::min(row[a], row[b]) - 20, "valley %d between %d and %d", valley, row[a], row[b]);
    const uint8_t* tail = &lines->fine[size_t(lines->frames - 1) * kFineBins];  // after the tones: silence
    int loudest = 0;
    for (int j = 0; j < kFineBins; ++j) loudest = std::max<int>(loudest, tail[j]);
    CHECK_MSG(loudest < 40, "the silent frame's loudest bin is %d", loudest);
}

TEST(the_live_fine_spectrum_equals_the_final_one) {
    const std::vector<float> sig = tsig::synth({{57, 0.0, 1.0, 0.4f, true}, {64, 0.3, 1.0, 0.3f, true}}, kRate, 1.0, true);
    const DecimatedSignal d = decimate(sig.data(), sig.size(), kRate);
    const std::shared_ptr<PitchLines> lines = computePitchLines(d.samples.data(), long(d.samples.size()), d.sampleRate, nullptr);
    LiveLines live;
    live.prepare(d.sampleRate, lines ? lines->frames + 4 : 8);
    live.advance(d.samples.data(), d.samples.size());
    CHECK(lines != nullptr && live.frames() > 10);
    if (!lines) return;
    int different = 0;
    for (int f = 0; f < std::min(live.frames(), lines->frames); ++f)
        for (int j = 0; j < kFineBins; ++j) different += live.fine()[size_t(f) * kFineBins + size_t(j)] != lines->fine[size_t(f) * kFineBins + size_t(j)] ? 1 : 0;
    CHECK_MSG(different == 0, "%d bins differ", different);
}

TEST(quick_grab_keeps_only_segments_of_at_least_120_ms) {
    CHECK(kQuickGrabMinNoteSec == 0.120);
    const std::vector<tsig::Tone> tones = {{60, 0.5, 0.58, 0.4f}, {64, 1.0, 1.30, 0.4f}};  // 80 ms and 300 ms
    const std::vector<float> sig = tsig::synth(tones, kRate, 2.0, true);
    const std::shared_ptr<Spectrogram> spec = computeSpectrogram(sig.data(), sig.size(), kRate);
    CHECK(spec != nullptr);
    if (!spec) return;
    Snapshot snap;
    snap.spectrogram = spec;
    const std::vector<NoteEvent> notes = quickGrabSnapshot(snap, 0.5f, FilterSettings{}, 0.0);
    int shortNotes = 0, has64 = 0;
    for (const NoteEvent& n : notes) {
        shortNotes += n.endSec - n.startSec < kQuickGrabMinNoteSec - 1e-9 ? 1 : 0;
        has64 += n.pitch == 64 ? 1 : 0;
    }
    CHECK_MSG(shortNotes == 0, "%d notes shorter than 120 ms", shortNotes);
    CHECK(has64 == 1);
}
