// Migration and threading of the pitch lines: a 4.0 state (no lines in it) loads and gets its lines in the background,
// a 4.0 editor state still decodes, the live lines grow while recording.
#include <cmath>
#include <cstring>

#include "TestKernel.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/PitchLines.h"
#include "core/TonetraceKernel.h"
#include "state/Crc32.h"
#include "state/StateCodec.h"
#include "state/UiSettings.h"

using namespace ttr;
using tk::kBlock;

namespace {
constexpr double kRate = 44100.0;

std::vector<uint8_t> editorStateV1() {  // byte for byte what Tonetrace 4.0 wrote
    std::vector<uint8_t> body;
    auto put = [&](const auto& v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); body.insert(body.end(), p, p + sizeof(v)); };
    put(0.6f); put(0.4f); put(2.0f); put(0.25f);       // gain, range, speed, quick grab
    put(int32_t(2));                                    // snap 1/8
    put(uint8_t(0)); put(uint8_t(1)); put(uint8_t(1));  // hide drums off, magnet on, sync on
    put(3.5); put(120.0);                               // scroll
    put(int32_t(1000)); put(int32_t(700));
    std::vector<uint8_t> out;
    auto putOut = [&](uint32_t v) { const auto* p = reinterpret_cast<const uint8_t*>(&v); out.insert(out.end(), p, p + 4); };
    putOut(0x31555454u); putOut(1u); putOut(uint32_t(body.size())); putOut(crc32(body.data(), body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}
}  // namespace

TEST(state_v40_loads_with_notes_and_gets_pitch_lines_in_the_background) {
    TonetraceKernel source;
    source.prepare(kRate, kBlock);
    CHECK(tk::record(source, tsig::synth({{69, 0.5, 2.0, 0.5f}}, kRate, 2.5, true)));
    source.setParam(kParamSnap, 3.0f);  // a 4.0 default
    source.editNotes({makeNote(69, 0.5, 2.0, source.timeGrid(), true)});
    // the encoder is the 4.0 format (no lines in the state): strip them like a 4.0 file has none
    Snapshot withoutLines = *source.snapshot();
    withoutLines.lines.reset();
    const std::vector<uint8_t> blob = encodeState(source.allParams(), &withoutLines, false);
    source.deactivate();

    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    CHECK(k.loadState(blob.data(), blob.size()));
    CHECK(k.snapshot() && k.snapshot()->notes.size() == 1 && k.snapshot()->notes[0].pitch == 69);
    CHECK(k.param(kParamSnap) == 3.0f);
    CHECK(k.snapshot()->hasRecording());
    CHECK(tk::waitFor([&] { return k.snapshot() && k.snapshot()->lines != nullptr; }));
    CHECK(k.snapshot()->notes.size() == 1);  // publishing the lines keeps the notes
    const SnapshotPtr snap = k.snapshot();
    if (snap->lines) {
        const LineHit hit = snap->lines->hitAt(1.2, 69.0, 0.5);
        CHECK(hit.track >= 0);
        const NoteEvent n = snap->lines->noteForTrack(hit.track, 0.0);
        CHECK(n.pitch == 69 && std::fabs(n.startSec - 0.5) < 0.03);
    }
    k.deactivate();
}

TEST(state_v40_without_audio_falls_back_without_lines) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    Snapshot onlyNotes;
    onlyNotes.notes = {makeNote(60, 0.0, 1.0, k.timeGrid(), true)};
    const std::vector<uint8_t> blob = encodeState(defaultParams(), &onlyNotes, false);
    CHECK(k.loadState(blob.data(), blob.size()));
    CHECK(k.snapshot()->notes.size() == 1 && k.snapshot()->lines == nullptr);
    k.deactivate();
}

TEST(ui_settings_of_version_4_0_still_decode_with_new_defaults) {
    const std::vector<uint8_t> v1 = editorStateV1();
    UiSettings s;
    std::string reason;
    CHECK_MSG(decodeUiSettings(v1.data(), v1.size(), &s, &reason), "%s", reason.c_str());
    CHECK(s.snap == 2 && s.speed == 2.0f && !s.hideDrums && s.syncToHost && s.width == kMinWindowWidth && s.height == 700);
    CHECK(!s.fundamentals && s.alignToSample && s.tuningTrim == 0.0f);
    CHECK(s.isSpectrum && !s.grabOnClick && !s.samplerOnDrag);  // the 4.3 fields take their defaults
    s.fundamentals = true;
    s.tuningTrim = -12.5f;
    const std::vector<uint8_t> v2 = encodeUiSettings(s);
    UiSettings back;
    CHECK(decodeUiSettings(v2.data(), v2.size(), &back) && back == s);
    UiSettings wild = s;
    wild.tuningTrim = 900.0f;
    CHECK(sanitized(wild).tuningTrim == 50.0f);
}

TEST(ui_settings_wave_toggle_defaults_on_survives_the_state_and_old_states) {
    UiSettings s;
    CHECK(s.showWave);  // default: the waveform lane is visible
    s.showWave = false;
    s.tuningTrim = 7.0f;
    const std::vector<uint8_t> blob = encodeUiSettings(s);
    UiSettings back;
    std::string reason;
    CHECK_MSG(decodeUiSettings(blob.data(), blob.size(), &back, &reason), "%s", reason.c_str());
    CHECK(!back.showWave && back.tuningTrim == 7.0f && back == s);
    s.showWave = true;
    const std::vector<uint8_t> shown = encodeUiSettings(s);
    CHECK(decodeUiSettings(shown.data(), shown.size(), &back) && back.showWave);
    // a 4.0 and a 4.1 editor state know no wave toggle: the lane stays visible
    const std::vector<uint8_t> v1 = editorStateV1();
    CHECK(decodeUiSettings(v1.data(), v1.size(), &back) && back.showWave);
    std::vector<uint8_t> v2(blob.begin(), blob.end() - 1);  // 4.1 wrote the same body without the last byte, version 2
    v2[4] = 2;
    const uint32_t length = uint32_t(v2.size() - 16);
    std::memcpy(&v2[8], &length, 4);
    const uint32_t crc = crc32(v2.data() + 16, length);
    std::memcpy(&v2[12], &crc, 4);
    UiSettings old;
    CHECK_MSG(decodeUiSettings(v2.data(), v2.size(), &old, &reason), "%s", reason.c_str());
    CHECK(old.showWave && old.tuningTrim == 7.0f);
    // the view settings of the host parameters do not touch it
    ParamValues params = defaultParams();
    CHECK(uiSettingsFromParams(params, false).showWave);
}

TEST(live_pitch_lines_grow_while_recording_and_finish_with_a_tuning) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    const double hz = 440.0 * std::pow(2.0, 0.20 / 12.0);
    const auto sig = tsig::synthHz({{hz, 0.2, 5.0, 0.5f, true}}, kRate, 5.5);
    k.setParam(kParamRecord, 1.0f);
    tk::run(k, sig, 0, size_t(4.0 * kRate));
    CHECK(tk::waitFor([&] { return k.mapView().lineFrames > 60; }));
    const MapView live = k.mapView();
    CHECK(live.linePoints != nullptr && live.lineCounts != nullptr && live.linePeak > 0.0f && live.lines == nullptr);
    tk::run(k, sig, size_t(4.0 * kRate), sig.size());
    k.setParam(kParamRecord, 0.0f);
    tk::run(k, tk::silence(2));
    CHECK(tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot() && k.snapshot()->lines; }));
    bool reliable = false;
    const double tuning = k.detectedTuningCents(&reliable);
    std::printf("    tuning of the +20 ct saw: %+.2f ct\n", tuning);
    CHECK(reliable && std::fabs(tuning - 20.0) <= 3.0);
    k.setLineView({true, true, 5.0f});
    CHECK(std::fabs(k.tuningOffsetCents() - (tuning + 5.0)) < 1e-6);
    k.setLineView({true, false, 0.0f});
    CHECK(k.tuningOffsetCents() == 0.0);
    k.setLineView({true, true, 500.0f});
    CHECK(k.lineView().trimCents == kMaxTrimCents);
    k.deactivate();
}
