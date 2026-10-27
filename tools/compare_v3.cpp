// Accuracy comparison of the Quick Grab analysis: synthetic mixes with ground truth, or a real file without.
// Built twice by tools/compare_v3.sh: against the v3 sources (-DTT_V3) and against the current tree. Both runs use the same
// generator, so the numbers are directly comparable.
//   compare_v3 mixes             one line per mix and Quick Grab amount: name amount F1 precision recall pitchAcc notes short
//   compare_v3 file <wav> <sec>  notes and short notes (< 120 ms) of the first <sec> seconds of a WAV file (read only)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "TestOldMixes.h"
#include "TestWav.h"
#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"

using namespace ttr;

namespace {

constexpr double kSampleRate = oldmix::kSampleRate;
constexpr double kOnsetTolSec = 0.050;
constexpr double kShortNoteSec = 0.120;

using oldmix::Truth;
using oldmix::Mix;
using oldmix::buildMixes;

std::vector<NoteEvent> grab(const std::vector<float>& audio, float amount) {
    const std::shared_ptr<Spectrogram> spec = computeSpectrogram(audio.data(), audio.size(), kSampleRate);
    if (!spec) return {};
    FilterSettings filter;
    filter.hideDrums = true;
#ifdef TT_V3
    return quickGrabNotes(*spec, amount, filter);
#else
    Snapshot snap;
    snap.spectrogram = spec;
    return quickGrabSnapshot(snap, amount, filter, 0.0);
#endif
}

struct Score { double f1 = 0, precision = 0, recall = 0, pitchAccuracy = 0; int notes = 0, shortNotes = 0; };

Score score(const std::vector<Truth>& truth, const std::vector<NoteEvent>& found) {
    Score s;
    s.notes = int(found.size());
    for (const NoteEvent& n : found) s.shortNotes += n.endSec - n.startSec < kShortNoteSec ? 1 : 0;
    std::vector<char> used(found.size(), 0);
    int hits = 0, onsetHits = 0, exactOnsetHits = 0;
    for (const Truth& t : truth) {
        int exact = -1, near = -1;
        for (size_t i = 0; i < found.size(); ++i) {
            if (used[i] || std::fabs(found[i].startSec - t.start) > kOnsetTolSec) continue;
            if (found[i].pitch == t.pitch) { exact = int(i); break; }
            if (std::abs(found[i].pitch - t.pitch) <= 1 && near < 0) near = int(i);
        }
        if (exact >= 0) { used[size_t(exact)] = 1; ++hits; ++onsetHits; ++exactOnsetHits; }
        else if (near >= 0) { used[size_t(near)] = 1; ++onsetHits; }
    }
    s.precision = found.empty() ? 0.0 : double(hits) / double(found.size());
    s.recall = truth.empty() ? 0.0 : double(hits) / double(truth.size());
    s.f1 = s.precision + s.recall > 0 ? 2.0 * s.precision * s.recall / (s.precision + s.recall) : 0.0;
    s.pitchAccuracy = onsetHits > 0 ? double(exactOnsetHits) / double(onsetHits) : 0.0;
    return s;
}

int runFile(const char* path, double seconds) {
    std::vector<float> audio;
    const double rate = twav::readWav(path, 0.0, seconds, &audio);
    if (rate <= 0.0) { std::fprintf(stderr, "cannot read %s\n", path); return 1; }
    const std::shared_ptr<Spectrogram> spec = computeSpectrogram(audio.data(), audio.size(), rate);
    if (!spec) { std::printf("no usable signal\n"); return 0; }
    FilterSettings filter;
    filter.hideDrums = true;
    for (const float amount : {0.0f, 0.5f}) {
#ifdef TT_V3
        const std::vector<NoteEvent> notes = quickGrabNotes(*spec, amount, filter);
#else
        Snapshot snap;
        snap.spectrogram = spec;
        const std::vector<NoteEvent> notes = quickGrabSnapshot(snap, amount, filter, 0.0);
#endif
        int shortNotes = 0;
        for (const NoteEvent& n : notes) shortNotes += n.endSec - n.startSec < kShortNoteSec ? 1 : 0;
        std::printf("real amount=%.1f notes=%zu short=%d share=%.3f\n", double(amount), notes.size(), shortNotes, notes.empty() ? 0.0 : double(shortNotes) / double(notes.size()));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 4 && std::strcmp(argv[1], "file") == 0) return runFile(argv[2], std::atof(argv[3]));
    for (const Mix& mix : buildMixes())
        for (const float amount : {0.0f, 0.5f, 1.0f}) {
            const Score s = score(mix.truth, grab(mix.audio, amount));
            std::printf("%s|%.1f|%.4f|%.4f|%.4f|%.4f|%d|%d\n", mix.name.c_str(), double(amount), s.f1, s.precision, s.recall, s.pitchAccuracy, s.notes, s.shortNotes);
        }
    return 0;
}
