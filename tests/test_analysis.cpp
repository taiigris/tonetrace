#include <algorithm>
#include <cmath>
#include <set>

#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/KeyEstimator.h"
#include "analysis/NoteAnalyzer.h"
#include "analysis/Spectrogram.h"

using namespace ttr;

namespace {

FilterSettings plain() {
    FilterSettings f;
    f.hideDrums = false;
    return f;
}

std::shared_ptr<Spectrogram> spectrogramOf(const std::vector<float>& sig, double sr) { return computeSpectrogram(sig.data(), sig.size(), sr); }

std::vector<NoteEvent> detect(const std::vector<float>& sig, double sr, float sens = 0.5f) {
    const auto spec = spectrogramOf(sig, sr);
    if (!spec) return {};
    const auto sal = computeSalience(*spec, plain());
    return sal ? extractNotes(*sal, sens, *spec, plain()) : std::vector<NoteEvent>{};
}

std::set<int> pitchSet(const std::vector<NoteEvent>& notes) {
    std::set<int> s;
    for (const auto& n : notes) s.insert(n.pitch);
    return s;
}

void dump(const std::vector<NoteEvent>& notes) {
    for (const auto& n : notes) std::printf("      pitch %d  %.3f-%.3f  s=%.2f\n", n.pitch, n.startSec, n.endSec, n.strength);
}

bool hasNote(const std::vector<NoteEvent>& notes, int pitch, double start, double end, double tol = 0.05) {
    return std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& n) {
        return n.pitch == pitch && std::fabs(n.startSec - start) <= tol && std::fabs(n.endSec - end) <= tol;
    });
}

void expectChord(bool saw, double sr) {
    const auto sig = tsig::synth({{57, 0, 2, 0.5f}, {61, 0, 2, 0.5f}, {64, 0, 2, 0.5f}}, sr, 2.0, saw);
    const auto notes = detect(sig, sr);
    if (pitchSet(notes) != std::set<int>{57, 61, 64} || notes.size() != 3) dump(notes);
    CHECK(pitchSet(notes) == (std::set<int>{57, 61, 64}));
    CHECK(notes.size() == 3);
    for (int p : {57, 61, 64}) CHECK(hasNote(notes, p, 0.0, 2.0));
}

}  // namespace

TEST(sine_chord_44k) { expectChord(false, 44100.0); }
TEST(saw_chord_44k) { expectChord(true, 44100.0); }
TEST(saw_chord_48k) { expectChord(true, 48000.0); }
TEST(saw_chord_96k) { expectChord(true, 96000.0); }

TEST(chord_then_melody) {
    for (bool saw : {false, true}) {
        const auto tones = tsig::chordThenMelody();
        const auto notes = detect(tsig::synth(tones, 44100.0, 6.5, saw), 44100.0);
        if (notes.size() != tones.size()) dump(notes);
        CHECK_MSG(notes.size() == tones.size(), "saw=%d got %zu expected %zu", saw, notes.size(), tones.size());
        for (const auto& t : tones) CHECK_MSG(hasNote(notes, t.pitch, t.start, t.end), "saw=%d missing pitch %d", saw, t.pitch);
    }
}

TEST(no_octave_errors_single_saw_notes) {
    for (int pitch : {45, 52, 57, 69, 76, 81}) {
        const auto notes = detect(tsig::synth({{pitch, 0.2, 1.7, 0.6f}}, 44100.0, 2.0, true), 44100.0);
        if (pitchSet(notes) != std::set<int>{pitch}) dump(notes);
        CHECK_MSG(pitchSet(notes) == std::set<int>{pitch}, "pitch %d", pitch);
    }
}

TEST(octave_pair_of_sines_both_found) {
    const auto notes = detect(tsig::synth({{57, 0, 2, 0.5f}, {69, 0, 2, 0.5f}}, 44100.0, 2.0, false), 44100.0);
    CHECK(pitchSet(notes) == (std::set<int>{57, 69}));
}

TEST(fifth_pair_of_saws) {
    const auto notes = detect(tsig::synth({{52, 0, 2, 0.5f}, {59, 0, 2, 0.5f}}, 44100.0, 2.0, true), 44100.0);
    if (pitchSet(notes) != std::set<int>{52, 59}) dump(notes);
    CHECK(pitchSet(notes) == (std::set<int>{52, 59}));
}

TEST(silence_gives_no_notes) {
    CHECK(detect(std::vector<float>(44100, 0.0f), 44100.0).empty());
}

TEST(sensitivity_reveals_quiet_note_without_reanalysis) {
    const auto sig = tsig::synth({{57, 0, 2, 0.8f}, {64, 0, 2, 0.8f * 0.04f}}, 44100.0, 2.0, false);  // -28 dB
    const auto spec = spectrogramOf(sig, 44100.0);
    CHECK(spec != nullptr);
    const auto sal = computeSalience(*spec, plain());
    CHECK(pitchSet(extractNotes(*sal, 0.0f, *spec, plain())) == std::set<int>{57});
    CHECK(pitchSet(extractNotes(*sal, 1.0f, *spec, plain())) == (std::set<int>{57, 64}));
}

TEST(short_gaps_are_bridged_long_gaps_split) {
    const auto sig = tsig::synth({{64, 0.5, 1.0, 0.5f}, {64, 1.03, 1.5, 0.5f}, {67, 0.5, 0.9, 0.5f}, {67, 1.4, 1.9, 0.5f}}, 44100.0, 2.0, false);
    const auto notes = detect(sig, 44100.0);
    CHECK(hasNote(notes, 64, 0.5, 1.5));
    CHECK(hasNote(notes, 67, 0.5, 0.9));
    CHECK(hasNote(notes, 67, 1.4, 1.9));
    CHECK(notes.size() == 3);
}

TEST(key_a_minor) {
    std::vector<NoteEvent> notes;
    double t = 0;
    for (int pc : {9, 9, 9, 0, 4, 4, 2, 5, 11, 7}) { notes.push_back({60 + (pc % 12), t, t + 1.0, 0.8f, false}); t += 1; }
    for (int i = 0; i < 4; ++i) { notes.push_back({57, t, t + 1.0, 0.9f, false}); t += 1; }
    const KeyResult k = estimateKey(pitchClassWeights(notes));
    CHECK(k.root == 9 && k.minor);
    CHECK(keyName(k) == "A minor");
}

TEST(key_c_major_and_detected_from_audio) {
    std::vector<NoteEvent> notes;
    double t = 0;
    for (int p : {60, 62, 64, 65, 67, 69, 71, 72, 60, 64, 67, 60, 67, 60}) { notes.push_back({p, t, t + 0.5, 0.8f, false}); t += 0.5; }
    const KeyResult k = estimateKey(pitchClassWeights(notes));
    CHECK(k.root == 0 && !k.minor);
    // From audio: A minor arpeggio melody
    const auto sig = tsig::synth({{57, 0, 1, .5f}, {60, 1, 2, .5f}, {64, 2, 3, .5f}, {57, 3, 4, .5f}, {62, 4, 5, .5f}, {57, 5, 7, .5f}}, 44100.0, 7.0, true);
    const auto found = detect(sig, 44100.0);
    CHECK(estimateKey(pitchClassWeights(found)).root == 9);
}

TEST(saw_chord_stress) {
    const std::vector<std::vector<int>> chords = {
        {48, 52, 55, 60}, {50, 57, 66}, {53, 57, 60, 64}, {55, 59, 62, 65}, {60, 64, 67, 71, 74}, {45, 52, 57, 61}, {62, 65, 69}, {40, 47, 55}};
    for (const auto& chord : chords) {
        std::vector<tsig::Tone> tones;
        for (int p : chord) tones.push_back({p, 0.0, 1.5, 0.4f});
        const auto notes = detect(tsig::synth(tones, 44100.0, 1.5, true), 44100.0);
        const std::set<int> expected(chord.begin(), chord.end());
        if (pitchSet(notes) != expected) { std::printf("    chord starting %d:\n", chord[0]); dump(notes); }
        CHECK(pitchSet(notes) == expected);
    }
}
