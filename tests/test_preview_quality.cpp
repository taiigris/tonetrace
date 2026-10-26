// Studio quality of the preview: no clicks at start / stop / loop wrap, smoothed parameters, limiter, host sync.
#include <cmath>

#include "TestKernel.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "core/NoteEdit.h"
#include "core/TonetraceKernel.h"
#include "preview/PreviewSynth.h"

using namespace ttr;

namespace {

constexpr double kRate = 48000.0;
constexpr int kBlock = 64;

float maxJump(const std::vector<float>& v) {
    float m = 0.0f;
    for (size_t i = 1; i < v.size(); ++i) m = std::max(m, std::fabs(v[i] - v[i - 1]));
    return m;
}

float peak(const std::vector<float>& v) {
    float m = 0.0f;
    for (float x : v) m = std::max(m, std::fabs(x));
    return m;
}

std::vector<float> render(TonetraceKernel& k, int blocks) {
    std::vector<float> out, l(kBlock), r(kBlock);
    for (int b = 0; b < blocks; ++b) {
        std::fill(l.begin(), l.end(), 0.0f);
        r = l;
        float* ch[2] = {l.data(), r.data()};
        k.process(ch, 2, kBlock);
        out.insert(out.end(), l.begin(), l.end());
    }
    return out;
}

// A kernel with a recording of a held chord and a few notes, preview balance on the notes.
void setup(TonetraceKernel& k, float mix) {
    k.prepare(kRate, 512);
    k.setHostTransport(120.0, 8.0);
    CHECK(tk::record(k, tsig::synth({{57, 0, 2.0, 0.5f}, {64, 0, 2.0, 0.5f}}, kRate, 2.0, false)));  // pure sines: the natural slope is small, so a click stands out
    k.editNotes({makeNote(57, 0.0, 1.0, k.timeGrid(), true), makeNote(64, 0.5, 2.0, k.timeGrid(), true)});
    k.setParam(kParamMix, mix);
    k.setParam(kParamSoundVolume, 1.0f);
}

}  // namespace

TEST(preview_starts_and_stops_without_clicks) {
    TonetraceKernel k;
    setup(k, 0.5f);
    k.setPreviewStart(0.37);  // starts in the middle of a note and of the sample
    k.setParam(kParamPreview, 1.0f);
    const auto on = render(k, 30);
    CHECK_MSG(std::fabs(on[0]) < 1e-3f, "first sample %.5f", on[0]);                 // fade-in from silence
    CHECK_MSG(maxJump(std::vector<float>(on.begin(), on.begin() + 200)) < 0.08f, "start jump %.4f", maxJump(std::vector<float>(on.begin(), on.begin() + 200)));
    CHECK(peak(on) > 0.05f);
    const auto steady = std::vector<float>(on.begin() + 800, on.end());
    CHECK_MSG(maxJump(std::vector<float>(on.begin(), on.begin() + 200)) <= 2.0f * maxJump(steady) + 1e-3f, "start is not steeper than the steady signal");
    k.setParam(kParamPreview, 0.0f);
    const auto off = render(k, 8);
    CHECK_MSG(maxJump(off) < 0.08f, "stop jump %.4f", maxJump(off));
    CHECK_MSG(std::fabs(off[300]) == 0.0f, "silence after the fade-out");  // 300 samples >> 96-sample fade
    CHECK(std::fabs(off[95]) < 0.01f);                                      // the ramp reaches zero at the end of the fade
}

TEST(loop_wrap_is_click_free) {
    TonetraceKernel k;
    setup(k, 0.0f);  // sample only: a full-scale tone is the worst case for a cut
    k.setPreviewLoop(0.2, 0.5);
    k.setParam(kParamPreview, 1.0f);
    const auto out = render(k, 600);  // 0.8 s: several wraps of the 0.3 s loop
    CHECK_MSG(maxJump(out) < 0.15f, "wrap jump %.4f", maxJump(out));
    CHECK(peak(out) > 0.2f);
}

TEST(sound_and_mix_changes_are_smoothed) {
    TonetraceKernel k;
    setup(k, 1.0f);
    k.setParam(kParamPreview, 1.0f);
    render(k, 50);
    k.setParam(kParamSoundVolume, 0.0f);  // hard switch from full level to silence
    const auto down = render(k, 4);
    CHECK_MSG(maxJump(down) < 0.05f, "sound jump %.4f", maxJump(down));
    k.setParam(kParamSoundVolume, 1.0f);
    k.setParam(kParamMix, 0.0f);
    k.setParam(kParamSolo, 1.0f);
    const auto change = render(k, 4);
    CHECK_MSG(maxJump(change) < 0.2f, "mix/solo jump %.4f", maxJump(change));
}

TEST(preview_bus_is_soft_limited_and_never_clips) {
    TonetraceKernel k;
    k.prepare(kRate, 512);
    k.setHostTransport(120.0, 0.0);
    CHECK(tk::record(k, tsig::synth({{57, 0, 1.0, 0.95f, true}}, kRate, 1.0, false)));  // loud sample
    std::vector<NoteEvent> notes;
    for (int p = 48; p < 72; ++p) notes.push_back(makeNote(p, 0.0, 1.0, k.timeGrid(), false));  // 24 notes at once
    k.editNotes(notes);
    k.setParam(kParamMix, 0.5f);
    k.setParam(kParamSoundVolume, 1.0f);
    k.setParam(kParamSolo, 1.0f);
    k.setParam(kParamPreview, 1.0f);
    const auto out = render(k, 700);
    CHECK_MSG(peak(out) <= 1.0f, "peak %.4f", peak(out));
    CHECK(peak(out) > 0.5f);
    for (float v : out) CHECK(std::isfinite(v));
}

TEST(preview_follows_the_host_position_when_synced) {
    TonetraceKernel k;
    setup(k, 0.5f);
    const double startPpq = k.snapshot()->startPpq, bps = k.snapshot()->bpm / 60.0;
    k.setSyncToHost(true);
    k.setParam(kParamPreview, 1.0f);

    k.setHostTransport(120.0, startPpq + 1.0 * bps, false);  // transport stopped: preview stays silent
    CHECK(tk::rms(render(k, 20)) == 0.0);

    k.setHostTransport(120.0, startPpq + 1.0 * bps, true);  // playing at 1.0 s of the recording
    render(k, 4);
    CHECK_MSG(std::fabs(k.playheadSec() - 1.0) < 0.05, "playhead %.3f s", k.playheadSec());
    k.setHostTransport(120.0, startPpq + 1.5 * bps, true);  // the host jumped to 1.5 s
    render(k, 2);
    CHECK_MSG(std::fabs(k.playheadSec() - 1.5) < 0.05, "playhead %.3f s after the jump", k.playheadSec());
    k.setHostTransport(120.0, startPpq - 2.0, true);  // before the recording: nothing to play
    render(k, 4);                                       // (the fade-out of what was playing)
    CHECK(tk::rms(render(k, 20)) == 0.0);
    k.setSyncToHost(false);
    CHECK(tk::rms(render(k, 20)) > 0.001);  // free-running again
}

TEST(synth_voice_stealing_and_extreme_pitches_stay_finite) {
    PreviewSynth s;
    s.prepare(44100.0);
    std::vector<float> out(512, 0.0f);
    for (int i = 0; i < 100; ++i) {  // far more notes than voices
        s.noteOn(24 + i % 85, 1.0f, 0.5);
        s.render(out.data(), 64);
    }
    s.noteOn(108, 1.0f, 0.2);
    s.noteOn(24, 1.0f, 0.2);
    for (int i = 0; i < 400; ++i) { std::fill(out.begin(), out.end(), 0.0f); s.render(out.data(), 64); for (int j = 0; j < 64; ++j) CHECK(std::isfinite(out[size_t(j)])); }
    CHECK(maxJump(out) < 0.5f);
    for (int i = 0; i < 400; ++i) s.render(out.data(), 64);
    CHECK(!s.isActive());
}
