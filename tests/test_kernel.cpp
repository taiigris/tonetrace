#include <cmath>
#include <set>

#include "TestDrums.h"
#include "TestKernel.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "core/TonetraceKernel.h"

using namespace ttr;
using tk::kBlock;

namespace {

constexpr double kRate = 44100.0;

std::vector<float> chord(double length = 2.0) {
    return tsig::synth({{57, 0, length, 0.4f}, {61, 0, length, 0.4f}, {64, 0, length, 0.4f}}, kRate, length, true);
}

}  // namespace

TEST(kernel_record_fills_spectrogram_peaks_and_passes_audio_through) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    k.setHostTransport(100.0, 8.0);
    const auto sig = tsig::synth({{57, 0, 1, .5f}, {60, 1, 2, .5f}, {64, 2, 3, .5f}, {57, 3, 4, .5f}, {62, 4, 5, .5f}, {57, 5, 7, .5f}}, kRate, 7.0, true);

    const auto dry = tk::run(k, sig);  // record off: nothing captured, dry unchanged
    CHECK(dry == std::vector<float>(sig.begin(), sig.begin() + ptrdiff_t(dry.size())));
    CHECK(k.recordedSeconds() == 0.0 && !k.snapshot());

    k.setParam(kParamRecord, 1.0f);
    const auto recorded = tk::run(k, sig);
    CHECK(recorded == std::vector<float>(sig.begin(), sig.begin() + ptrdiff_t(recorded.size())));  // dry while recording
    CHECK(k.status() == KernelStatus::Recording);
    CHECK(tk::waitFor([&] { return k.mapView().frames > 40 && k.mapView().waveCount > 300; }, 3000));  // grows while recording
    const MapView live = k.mapView();
    CHECK(live.harmonic != nullptr && live.raw != nullptr && live.peak > 0.0f);  // harmonic part is computed live, a few frames behind
    CHECK(live.harmonicFrames > 0 && live.harmonicFrames <= live.frames);
    CHECK(k.recordedSeconds() > 6.9);

    k.setParam(kParamRecord, 0.0f);
    tk::run(k, tk::silence(2));
    CHECK(tk::waitFor([&] { return k.status() == KernelStatus::Idle && k.snapshot(); }));
    const SnapshotPtr snap = k.snapshot();
    CHECK(snap && snap->hasRecording());
    CHECK(snap && snap->notes.empty());  // nothing is detected automatically
    CHECK(snap && snap->bpm == 100.0 && snap->startPpq == 8.0);
    CHECK(snap && snap->lengthSec > 6.9 && snap->lengthSec < 7.2);
    CHECK(snap && snap->peaks && std::abs(snap->peaks->count() - int(snap->lengthSec * kPeaksPerSecond)) <= 2);
    CHECK(snap && snap->audio && snap->audio->samples.size() > 40000);
    CHECK(snap && snap->key.root == 9 && snap->key.minor);  // A minor from the recording's chroma
    const MapView done = k.mapView();
    CHECK(done.harmonic != nullptr && done.frames == snap->spectrogram->frames);

    const double length = k.recordedSeconds();  // record off again: the capture stays untouched
    tk::run(k, sig);
    CHECK(k.recordedSeconds() == length);
    k.deactivate();
}

TEST(kernel_draw_edit_undo_redo) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    CHECK(!k.canUndo() && !k.undo());
    k.setParam(kParamSnap, 3.0f);  // 1/16 grid (the default snap mode is Onset)
    const TimeGrid grid = k.timeGrid();
    k.editNotes(withNoteAdded({}, makeNote(69, 1.01, 1.49, grid, true)));
    CHECK(k.snapshot() && k.snapshot()->notes.size() == 1);  // notes can be drawn before any recording
    k.editNotes(withNoteAdded(k.snapshot()->notes, makeNote(72, 2.0, 2.5, grid, true)));
    k.editNotes(withSelection(k.snapshot()->notes, {0}, SelectMode::Replace));
    CHECK(k.snapshot()->notes.size() == 2 && selectedCount(k.snapshot()->notes) == 1);

    // a gesture: one checkpoint, many live updates, one undo step
    k.checkpoint();
    for (int i = 1; i <= 5; ++i) k.setNotes(withMoved(k.snapshot()->notes, {0}, 0.1, 0));
    CHECK(std::fabs(k.snapshot()->notes[0].startSec - 1.5) < 1e-9);
    CHECK(k.undo());
    CHECK(std::fabs(k.snapshot()->notes[0].startSec - 1.0) < 1e-9);
    CHECK(k.redo());
    CHECK(std::fabs(k.snapshot()->notes[0].startSec - 1.5) < 1e-9);
    while (k.undo()) {}
    CHECK(k.snapshot()->notes.empty() && k.canRedo() && !k.canUndo());
    while (k.redo()) {}
    CHECK(k.snapshot()->notes.size() == 2);
    k.editNotes({});
    CHECK(k.snapshot()->notes.empty() && k.canUndo());
    k.deactivate();
}

TEST(kernel_quick_grab_adds_the_strongest_lines_only_on_request) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    const auto tones = tsig::chordThenMelody();
    const auto sig = tsig::synth(tones, kRate, 6.5, true);
    CHECK(k.quickGrab() == 0);  // no recording yet
    CHECK(tk::record(k, sig));
    CHECK(k.snapshot()->notes.empty());  // recording alone creates no notes

    k.setParam(kParamQuickGrab, 0.5f);
    const int added = k.quickGrab();
    CHECK_MSG(added == int(tones.size()), "added %d expected %zu", added, tones.size());
    for (const auto& t : tones) {
        const auto& notes = k.snapshot()->notes;
        const bool found = std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& n) {
            return n.pitch == t.pitch && std::fabs(n.startSec - t.start) < 0.05 && std::fabs(n.endSec - t.end) < 0.05;
        });
        CHECK_MSG(found, "pitch %d missing", t.pitch);
    }
    CHECK(k.quickGrab() == 0);  // a second grab finds nothing new
    CHECK(k.undo() && k.snapshot()->notes.empty());
    k.deactivate();
}

TEST(kernel_quick_grab_amount_and_hide_drums) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    const double length = 6.0;
    auto sig = tdrum::render(tdrum::pattern(90.0, length), kRate, length);
    const auto quiet = tsig::synth({{69, 0.0, 5.8, 0.25f}, {73, 0.0, 5.8, 0.25f * 0.04f}}, kRate, length, true);  // 2nd line -28 dB
    for (size_t i = 0; i < sig.size(); ++i) sig[i] += quiet[i];
    CHECK(tk::record(k, sig));

    // 4.3: Quick Grab always works on the harmonic part; the Hide drums toggle only dims the picture
    k.setParam(kParamHideDrums, 0.0f);
    k.setParam(kParamQuickGrab, 1.0f);
    const int withToggleOff = k.quickGrab();
    std::set<int> pitches;
    for (const NoteEvent& n : k.snapshot()->notes) pitches.insert(n.pitch);
    k.undo();
    k.setParam(kParamHideDrums, 1.0f);
    const int withToggleOn = k.quickGrab();
    CHECK_MSG(withToggleOn == withToggleOff, "toggle on %d, off %d", withToggleOn, withToggleOff);
    CHECK_MSG(pitches == (std::set<int>{69, 73}), "the grabbed notes are the two tonal lines, no drum notes (%zu pitches)", pitches.size());
    k.undo();

    k.setParam(kParamQuickGrab, 0.0f);  // strongest lines only: the -28 dB line stays out
    const int strongest = k.quickGrab();
    k.undo();
    k.setParam(kParamQuickGrab, 1.0f);
    CHECK_MSG(k.quickGrab() > strongest, "amount 1 must find more than amount 0 (%d)", strongest);
    k.deactivate();
}

TEST(kernel_preview_plays_sample_and_notes) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    const auto sig = chord(2.0);
    CHECK(tk::record(k, sig));
    k.editNotes({makeNote(72, 0.5, 1.0, k.timeGrid(), true)});

    const auto silent = tk::silence(60);
    CHECK(tk::rms(tk::run(k, silent)) == 0.0);  // preview off: input passes untouched

    k.setParam(kParamPreview, 1.0f);
    k.setParam(kParamMix, 0.0f);  // sample only
    const auto sampleOnly = tk::run(k, silent, 0, 40 * kBlock);
    CHECK(tk::rms(sampleOnly) > 0.05);
    k.setParam(kParamPreview, 0.0f);
    tk::run(k, tk::silence(2));
    k.setParam(kParamPreview, 1.0f);
    k.setParam(kParamMix, 1.0f);  // notes only
    const auto notesOnly = tk::run(k, silent, 0, 60 * kBlock);
    CHECK(tk::rms(notesOnly) > 0.0 && tk::rms(notesOnly) < tk::rms(sampleOnly));
    CHECK(k.playheadSec() > 0.0);

    k.setParam(kParamMix, 0.5f);
    k.setParam(kParamSolo, 1.0f);  // host input is muted while previewing
    std::vector<float> loud(kBlock * 4, 0.5f);
    CHECK(tk::rms(tk::run(k, loud)) < 0.45);
    k.setParam(kParamSolo, 0.0f);
    CHECK(tk::rms(tk::run(k, loud)) > 0.45);
    k.deactivate();
}

TEST(kernel_preview_start_and_loop_region) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    CHECK(tk::record(k, chord(2.0)));
    k.setParam(kParamPreview, 1.0f);
    k.setPreviewStart(1.0);
    tk::run(k, tk::silence(2));
    CHECK(k.playheadSec() > 1.0 && k.playheadSec() < 1.1);
    k.setPreviewLoop(0.5, 0.8);
    double a = 0, b = 0;
    CHECK(k.previewLoop(&a, &b) && a == 0.5 && b == 0.8);
    for (int i = 0; i < 80; ++i) {  // 0.9 s: must stay inside the loop and wrap around
        tk::run(k, tk::silence(1));
        CHECK(k.playheadSec() >= 0.5 && k.playheadSec() <= 0.8 + 0.02);
    }
    k.setPreviewLoop(0.7, 0.71);  // too short: no loop
    CHECK(!k.previewLoop(nullptr, nullptr));
    k.deactivate();
}

TEST(kernel_state_roundtrip_with_spectrogram_peaks_notes_and_audio) {
    TonetraceKernel a;
    a.prepare(kRate, kBlock);
    CHECK(tk::record(a, chord(1.5)));
    a.editNotes({makeNote(57, 0.0, 1.0, a.timeGrid(), true), makeNote(64, 0.5, 1.0, a.timeGrid(), true)});
    a.setNotes(withSelection(a.snapshot()->notes, {1}, SelectMode::Replace));
    a.setParam(kParamSnap, 2.0f);
    a.setParam(kParamGain, 0.8f);
    const auto state = a.saveState();
    CHECK(state.size() < 6u * 1024u * 1024u);

    TonetraceKernel b;
    b.prepare(kRate, kBlock);
    CHECK(b.loadState(state.data(), state.size()));
    CHECK(b.param(kParamSnap) == 2.0f && b.param(kParamGain) == 0.8f);
    const SnapshotPtr sa = a.snapshot(), sb = b.snapshot();
    CHECK(sb && sb->notes.size() == 2 && selectedCount(sb->notes) == 1);
    CHECK(sb && sb->spectrogram && sb->spectrogram->frames == sa->spectrogram->frames);
    CHECK(sb && sb->peaks && sb->peaks->count() == sa->peaks->count());
    CHECK(sb && sb->audio && sb->audio->samples.size() == sa->audio->samples.size());
    CHECK(sb && sb->key.root == sa->key.root && sb->bpm == sa->bpm);
    CHECK(!b.canUndo());  // history is not part of the state

    // The restored audio plays in the preview (also when the state is loaded before the kernel is prepared).
    for (TonetraceKernel* kernel : {&b}) {
        kernel->setParam(kParamPreview, 1.0f);
        kernel->setParam(kParamMix, 0.0f);
        CHECK(tk::rms(tk::run(*kernel, tk::silence(30))) > 0.05);
    }
    TonetraceKernel c;
    CHECK(c.loadState(state.data(), state.size()));
    c.prepare(kRate, kBlock);
    c.setParam(kParamPreview, 1.0f);
    c.setParam(kParamMix, 0.0f);
    CHECK(tk::rms(tk::run(c, tk::silence(30))) > 0.05);

    CHECK(!b.loadState(state.data(), 5));
    uint8_t junk[16] = {0};
    CHECK(!b.loadState(junk, sizeof junk));
    a.deactivate(); b.deactivate(); c.deactivate();
}

TEST(kernel_reset_and_bypass_and_param_clamping) {
    TonetraceKernel k;
    k.prepare(kRate, kBlock);
    CHECK(tk::record(k, chord(1.0)));
    k.editNotes({makeNote(60, 0.0, 0.5, k.timeGrid(), true)});
    k.resetAll();
    CHECK(!k.snapshot() && k.recordedSeconds() == 0.0);
    CHECK(k.canUndo());  // Reset is one undo step of the whole document
    CHECK(k.undo() && k.snapshot() && k.snapshot()->hasRecording() && k.snapshot()->notes.size() == 1);
    CHECK(k.recordedSeconds() > 0.5);  // the preview audio is restored as well
    CHECK(k.redo() && !k.snapshot());
    k.resetAll();

    k.setParam(kParamBypass, 1.0f);  // bypass: no recording even with Record on, signal untouched
    k.setParam(kParamRecord, 1.0f);
    const auto sig = chord(0.5);
    CHECK(tk::run(k, sig) == std::vector<float>(sig.begin(), sig.begin() + ptrdiff_t(sig.size() / kBlock * kBlock)));
    CHECK(k.status() == KernelStatus::Idle && k.recordedSeconds() == 0.0);

    k.setParam(kParamSnap, 7.0f);
    CHECK(k.param(kParamSnap) == 5.0f);
    k.setParam(kParamSpeed, 100.0f);
    CHECK(k.param(kParamSpeed) == kSpeedMax);
    k.setParam(kParamGain, NAN);
    CHECK(k.param(kParamGain) == 0.5f);
    k.deactivate();
}

TEST(kernel_stops_recording_at_capacity_without_crash) {
    TonetraceKernel k;
    k.prepare(8000.0, kBlock);  // tiny rate keeps this fast: 120 s = 960k samples
    k.setParam(kParamRecord, 1.0f);
    for (int i = 0; i < 2200; ++i) {
        std::vector<float> copy(kBlock, 0.1f);
        float* ch[1] = {copy.data()};
        k.process(ch, 1, kBlock);
    }
    CHECK(k.recordedSeconds() <= kMaxRecordSeconds + 1e-6);
    CHECK(k.recordedSeconds() > kMaxRecordSeconds - 0.1);
    k.deactivate();
}
