// 4.4: held notes through drums and sidechain pumping, bass fundamental from long windows, tempo-aware minimum length.
#include <algorithm>
#include <cmath>

#include "TestMetrics.h"
#include "TestSignals.h"
#include "TestTechno.h"
#include "TestUtil.h"
#include "analysis/BeatHits.h"
#include "analysis/LowBand.h"
#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"
#include "analysis/SustainGrab.h"
#include "analysis/SustainRuns.h"
#include "core/Params.h"

using namespace ttr;

namespace {

constexpr double kRate = 44100.0;

std::vector<NoteEvent> grabMix(const ttech::Mix& mix, float amount, bool withLow = true) {
    const std::shared_ptr<Spectrogram> spec = computeSpectrogram(mix.audio.data(), mix.audio.size(), kRate);
    if (!spec) return {};
    Snapshot snap;
    auto edited = std::make_shared<Spectrogram>(*spec);
    if (!withLow) edited->low.clear();
    snap.spectrogram = edited;
    snap.bpm = mix.bpm;
    return quickGrabSnapshot(snap, amount, FilterSettings{}, 0.0);
}

tmetric::Score scoreOf(const ttech::Mix& mix, const std::vector<NoteEvent>& found) {
    std::vector<tmetric::Note> truth, pieces;
    for (const ttech::Truth& t : mix.truth) truth.push_back({t.pitch, t.start, t.end});
    for (const NoteEvent& n : found) pieces.push_back({n.pitch, n.startSec, n.endSec});
    return tmetric::score(truth, pieces);
}

}  // namespace

TEST(techno_mixes_keep_held_notes_in_one_piece) {
    for (const ttech::Recipe& recipe : ttech::recipes()) {
        const ttech::Mix mix = ttech::build(recipe);
        const tmetric::Score s = scoreOf(mix, grabMix(mix, 1.0f));
        std::printf("    %s: fragmentation %.2f, duration F1 %.2f, note F1 %.2f, short %.0f %%\n", recipe.name, s.frag, s.durationF1, s.noteF1, s.shortShare * 100.0);
        CHECK_MSG(s.frag <= 1.3, "%s fragmentation %.2f", recipe.name, s.frag);
        CHECK_MSG(s.durationF1 >= 0.6, "%s duration F1 %.2f", recipe.name, s.durationF1);
        CHECK_MSG(s.shortShare <= 0.1, "%s short share %.2f", recipe.name, s.shortShare);
    }
}

TEST(techno_bass_stays_one_note_across_kicks_and_pumping) {
    const ttech::Mix mix = ttech::build(ttech::recipes()[1]);
    const std::vector<NoteEvent> notes = grabMix(mix, 1.0f);
    const ttech::Truth* longest = nullptr;  // the longest held bass note of the mix
    for (const ttech::Truth& t : mix.truth)
        if (t.pitch < 40 && (!longest || t.end - t.start > longest->end - longest->start)) longest = &t;
    CHECK(longest != nullptr);
    if (!longest) return;
    int pieces = 0;
    double covered = 0.0;
    for (const NoteEvent& n : notes)
        if (n.pitch == longest->pitch && tmetric::overlap(n.startSec, n.endSec, longest->start, longest->end) > 0.5 * (n.endSec - n.startSec)) {
            ++pieces;
            covered += tmetric::overlap(n.startSec, n.endSec, longest->start, longest->end);
        }
    CHECK_MSG(pieces >= 1 && pieces <= 2, "%d pieces", pieces);
    CHECK_MSG(covered >= 0.8 * (longest->end - longest->start), "covered %.2f of %.2f s", covered, longest->end - longest->start);
}

TEST(techno_without_long_windows_still_grabs_the_bass_register) {  // restored states without audio: the 4.3 resolution below C3
    const ttech::Mix mix = ttech::build(ttech::recipes()[0]);
    const std::vector<NoteEvent> notes = grabMix(mix, 1.0f, false);
    CHECK(!notes.empty());
    const bool hasBass = std::any_of(notes.begin(), notes.end(), [](const NoteEvent& n) { return n.pitch < 48 && n.endSec - n.startSec > 1.0; });
    CHECK(hasBass);
}

TEST(low_band_separates_semitones_of_a_44_hz_bass) {
    const double rate = 22050.0;
    std::vector<float> tone(size_t(4 * rate));
    for (size_t i = 0; i < tone.size(); ++i) tone[i] = 0.5f * float(std::sin(2.0 * M_PI * tsig::midiToHz(29) * double(i) / rate));  // F1
    const int frames = int(tone.size()) / 512;
    const std::vector<float> low = computeLowEnergies(tone.data(), long(tone.size()), rate, frames, 512);
    const int mid = frames / 2;
    const float f1 = low[size_t(mid) * kLowNotes + size_t(29 - kLowFirstNote)];
    const float fSharp = low[size_t(mid) * kLowNotes + size_t(30 - kLowFirstNote)];
    const float e1 = low[size_t(mid) * kLowNotes + size_t(28 - kLowFirstNote)];
    CHECK_MSG(f1 > 1.5f * fSharp && f1 > 1.5f * e1, "F1 %.2f, F#1 %.2f, E1 %.2f", double(f1), double(fSharp), double(e1));
}

TEST(minimum_note_length_follows_the_tempo) {
    CHECK(std::fabs(minNoteSecForTempo(130.0) - 15.0 / 130.0) < 1e-9);
    CHECK(std::fabs(minNoteSecForTempo(0.0) - 0.120) < 1e-9);
    CHECK(std::fabs(minNoteSecForTempo(1000.0) - 0.120) < 1e-9);
}

TEST(hysteresis_keeps_a_pumped_note_and_valleys_split_a_restart) {
    std::vector<float> v(120, 0.0f);
    for (int f = 10; f < 110; ++f) v[size_t(f)] = 1.0f;
    for (int f = 40; f < 50; ++f) v[size_t(f)] = 0.3f + 0.07f * float(f - 40);  // -10 dB pump dip, released over ten frames
    const std::vector<char> noHits(v.size(), 0);
    std::vector<Run> runs = valleyRuns(v, v, v, noHits, 10, hysteresisRuns(v, 0.05f, 0.02f));
    CHECK(runs.size() == 1 && runs[0].first == 10 && runs[0].last == 109);
    for (int f = 70; f < 74; ++f) v[size_t(f)] = 0.0f;   // silence between two notes
    runs = bridgeRuns(v, v, v, noHits, valleyRuns(v, v, v, noHits, 10, hysteresisRuns(v, 0.05f, 0.02f)), 1, 10, 0);
    CHECK_MSG(runs.size() == 2, "%zu runs", runs.size());
}

TEST(beat_pulse_is_a_kick_and_a_pumped_hold_is_not) {
    const double hop = 0.02, bpm = 120.0;  // beat = 0.5 s = 25 frames
    std::vector<float> kick(400), pumped(400);
    for (size_t f = 0; f < kick.size(); ++f) {
        const double phase = std::fmod(double(f) * hop, 0.5) / 0.5;
        kick[f] = float(std::exp(-3.0 * phase));                    // loud right after the beat, decayed before the next
        pumped[f] = float(1.0 - 0.7 * std::exp(-6.0 * phase));      // low at the beat, back up before the next
    }
    const BeatGrid grid{60.0 / bpm, 0.0};
    CHECK(isBeatPulse(kick, 0, 399, hop, grid, 2));
    CHECK(!isBeatPulse(pumped, 0, 399, hop, grid, 2));
    CHECK(!isBeatPulse(kick, 0, 399, hop, BeatGrid{}, 2));  // no usable grid: nothing is judged
}

TEST(parameter_ids_stay_stable_for_saved_projects) {
    CHECK(kParamRecord == 0 && kParamGain == 1 && kParamRange == 2 && kParamSpeed == 3 && kParamHideDrums == 4 && kParamSnap == 5);
    CHECK(kParamMagnet == 6 && kParamQuickGrab == 7 && kParamSoundVolume == 8 && kParamMix == 9 && kParamSolo == 10);
    CHECK(kParamPreview == 11 && kParamBypass == 12 && kNumParams == 13);
}

TEST(octave_note_with_its_own_onsets_stays_a_note_beside_a_held_bass) {
    const double length = 5.0;
    const std::vector<tsig::HzTone> bass = {{tsig::midiToHz(29), 0.2, 4.6, 0.30f, true, 0.0, 5.0, 0.0}};  // saw: its 2nd partial is the octave
    std::vector<float> sig = tsig::synthHz(bass, kRate, length);
    const std::vector<float> octave = tsig::synthHz({{tsig::midiToHz(41), 1.0, 2.0, 0.30f}, {tsig::midiToHz(41), 2.4, 3.6, 0.30f}}, kRate, length);
    std::vector<float> plain = sig;
    for (size_t i = 0; i < sig.size(); ++i) sig[i] += octave[i];
    auto grab = [&](const std::vector<float>& audio) {
        const std::shared_ptr<Spectrogram> spec = computeSpectrogram(audio.data(), audio.size(), kRate);
        Snapshot snap;
        snap.spectrogram = spec;
        snap.bpm = 120.0;
        return quickGrabSnapshot(snap, 1.0f, FilterSettings{}, 0.0);
    };
    auto covered = [](const std::vector<NoteEvent>& notes, int pitch, double a, double b) {
        double sum = 0.0;
        for (const NoteEvent& n : notes) sum += n.pitch == pitch ? tmetric::overlap(n.startSec, n.endSec, a, b) : 0.0;
        return sum;
    };
    const std::vector<NoteEvent> both = grab(sig), alone = grab(plain);
    CHECK_MSG(covered(both, 41, 1.0, 2.0) + covered(both, 41, 2.4, 3.6) >= 0.7 * 2.2, "octave covered %.2f s", covered(both, 41, 1.0, 2.0) + covered(both, 41, 2.4, 3.6));
    CHECK_MSG(covered(both, 41, 2.0, 2.4) < 0.2, "the gap of the octave note is kept (%.2f s)", covered(both, 41, 2.0, 2.4));
    CHECK_MSG(covered(alone, 41, 0.0, 5.0) < 0.5, "the bass alone gets no octave note (%.2f s)", covered(alone, 41, 0.0, 5.0));
    CHECK(covered(both, 29, 0.5, 4.5) > 3.0);
}
