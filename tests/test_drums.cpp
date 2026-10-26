#include <algorithm>
#include <cmath>
#include <set>

#include "TestDrums.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/NoteAnalyzer.h"
#include "analysis/Spectrogram.h"

using namespace ttr;

namespace {

constexpr double kSr = 44100.0;
constexpr double kLen = 8.0;
constexpr double kBpm = 90.0;
constexpr int kKickLow = 28, kKickHigh = 50;   // pitches a 150 -> 45 Hz kick passes through
constexpr int kSnarePitch = 54;                 // 190 Hz

FilterSettings hideDrums(float amount = 0.6f) {
    FilterSettings f;
    f.hideDrums = true;
    f.drumFilter = amount;
    return f;
}

std::vector<NoteEvent> analyse(const std::vector<float>& sig, const FilterSettings& filter, float sens = 0.5f) {
    const auto spec = computeSpectrogram(sig.data(), sig.size(), kSr);
    if (!spec) return {};
    const auto sal = computeSalience(*spec, filter);
    return sal ? extractNotes(*sal, sens, *spec, filter) : std::vector<NoteEvent>{};
}

void dump(const std::vector<NoteEvent>& notes) {
    for (const auto& n : notes)
        std::printf("      pitch %d  %.3f-%.3f  s=%.2f ton=%.2f sus=%.2f\n", n.pitch, n.startSec, n.endSec, n.strength, n.tonality, n.sustain);
}

std::vector<float> mix(std::vector<float> a, const std::vector<float>& b) {
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) a[i] += b[i];
    return a;
}

// Saw melody + chord over the drum loop; drums are louder than the melody.
std::vector<tsig::Tone> melodyAndChord() {
    std::vector<tsig::Tone> t = {{57, 0.0, 3.9, 0.22f}, {61, 0.0, 3.9, 0.22f}, {64, 0.0, 3.9, 0.22f}};
    const double beat = 60.0 / kBpm;
    const int melody[] = {69, 72, 76, 74, 72, 69};
    double at = 4.0;
    for (int p : melody) { t.push_back({p, at, at + 0.8 * beat, 0.25f}); at += beat; }
    return t;
}

}  // namespace

TEST(drums_only_gives_at_most_one_note) {
    const auto drums = tdrum::render(tdrum::pattern(kBpm, kLen), kSr, kLen);
    const auto notes = analyse(drums, hideDrums());
    if (notes.size() > 1) dump(notes);
    CHECK_MSG(notes.size() <= 1, "got %zu notes", notes.size());
}

TEST(melody_over_drums_keeps_melody_and_drops_drums) {
    const auto hits = tdrum::pattern(kBpm, kLen);
    const auto tones = melodyAndChord();
    const auto sig = mix(tdrum::render(hits, kSr, kLen), tsig::synth(tones, kSr, kLen, true));
    const auto notes = analyse(sig, hideDrums());
    for (const auto& t : tones) {
        const bool found = std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& n) {
            return n.pitch == t.pitch && n.startSec < t.end && n.endSec > t.start && n.endSec - n.startSec > 0.3 * (t.end - t.start);
        });
        CHECK_MSG(found, "melody/chord pitch %d (%.2f-%.2f) missing", t.pitch, t.start, t.end);
    }
    bool dumped = false;
    for (const auto& n : notes) {
        const bool onDrumPitch = (n.pitch >= kKickLow && n.pitch <= kKickHigh) || std::abs(n.pitch - kSnarePitch) <= 1;
        bool shortOnHit = false;
        for (const auto& h : hits) shortOnHit |= std::fabs(n.startSec - h.time) < 0.06 && n.endSec - n.startSec < 0.15;
        CHECK_MSG(!onDrumPitch, "note on drum pitch %d at %.2f", n.pitch, n.startSec);
        CHECK_MSG(!shortOnHit, "short note on drum onset: pitch %d at %.2f", n.pitch, n.startSec);
        if ((onDrumPitch || shortOnHit) && !dumped) { dump(notes); dumped = true; }
    }
}

TEST(held_808_with_click_survives_as_one_note) {
    const auto sig = tdrum::render808(kSr, 2.0, 55.0, 1.5);
    const auto notes = analyse(sig, hideDrums());
    if (notes.size() != 1) dump(notes);
    CHECK_MSG(notes.size() == 1, "got %zu notes", notes.size());
    if (!notes.empty()) {
        CHECK(notes[0].pitch == 33);
        CHECK(notes[0].endSec - notes[0].startSec > 1.0);
    }
}

TEST(hide_drums_off_keeps_the_drum_notes) {
    const auto sig = mix(tdrum::render(tdrum::pattern(kBpm, kLen), kSr, kLen), tsig::synth(melodyAndChord(), kSr, kLen, true));
    FilterSettings off;
    off.hideDrums = false;
    CHECK(analyse(sig, off).size() > 5);
}

TEST(drum_filter_amount_changes_result_without_reanalysis) {
    const auto sig = mix(tdrum::render(tdrum::pattern(kBpm, kLen), kSr, kLen), tsig::synth(melodyAndChord(), kSr, kLen, true));
    const auto spec = computeSpectrogram(sig.data(), sig.size(), kSr);
    CHECK(spec != nullptr);
    size_t counts[2];
    int i = 0;
    for (bool on : {false, true}) {
        FilterSettings f = hideDrums();
        f.hideDrums = on;
        const auto sal = computeSalience(*spec, f);
        counts[i++] = extractNotes(*sal, 0.5f, *spec, f).size();
    }
    CHECK_MSG(counts[1] < counts[0], "off %zu on %zu", counts[0], counts[1]);
}

TEST(hide_drums_keeps_clean_melody_and_chord_unchanged) {
    const auto tones = tsig::chordThenMelody();
    const auto sig = tsig::synth(tones, kSr, 6.5, true);
    const auto notes = analyse(sig, hideDrums());
    if (notes.size() != tones.size()) dump(notes);
    CHECK_MSG(notes.size() == tones.size(), "got %zu expected %zu", notes.size(), tones.size());
    for (const auto& t : tones) {
        const bool found = std::any_of(notes.begin(), notes.end(), [&](const NoteEvent& n) {
            return n.pitch == t.pitch && std::fabs(n.startSec - t.start) < 0.05 && std::fabs(n.endSec - t.end) < 0.05;
        });
        CHECK_MSG(found, "pitch %d (%.2f-%.2f) missing or misplaced", t.pitch, t.start, t.end);
    }
}
