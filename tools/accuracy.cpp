// Accuracy of Quick Grab on synthetic mixes with ground truth (old mixes and techno mixes) and on a real track window.
// Built by tools/accuracy.sh against a source tree (the 4.3 baseline or this tree).
//   accuracy mixes                                  one line per mix and Quick Grab amount: set|name|amount|noteF1|durationF1|frag|short|pieces
//   accuracy track <wav> <startSec> <beats> [bpm]   reference-window rows (read only); bpm 0 / missing = estimated
//   accuracy scan <wav> [bpm]                       scans the track in 20-beat windows and prints the best ones
//   accuracy export <dir>                           writes every mix as <dir>/<set>-<name>.wav (float32 mono) plus .bpm
//   accuracy score-mixes <dir>                      scores external notes <dir>/<set>-<name>.notes (pitch|start|end per line)
//   accuracy score-track <notes> <windowSec>        reference-window rows for external notes
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "TestMetrics.h"
#include "TestOldMixes.h"
#include "TestTechno.h"
#include "TestWav.h"
#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"

using namespace ttr;

namespace {

std::vector<NoteEvent> grab(const std::vector<float>& audio, double rate, double bpm, float amount) {
    const std::shared_ptr<Spectrogram> spec = computeSpectrogram(audio.data(), audio.size(), rate);
    if (!spec) return {};
    FilterSettings filter;
    filter.hideDrums = true;
    Snapshot snap;
    snap.spectrogram = spec;
    snap.bpm = bpm;
    return quickGrabSnapshot(snap, amount, filter, 0.0);
}

std::vector<tmetric::Note> toNotes(const std::vector<NoteEvent>& notes) {
    std::vector<tmetric::Note> out;
    for (const NoteEvent& n : notes) out.push_back({n.pitch, n.startSec, n.endSec});
    return out;
}

void report(const char* set, const std::string& name, const std::vector<tmetric::Note>& truth, const std::vector<float>& audio, double bpm) {
    for (const float amount : {0.0f, 0.5f, 1.0f}) {
        const tmetric::Score s = tmetric::score(truth, toNotes(grab(audio, oldmix::kSampleRate, bpm, amount)));
        std::printf("%s|%s|%.1f|%.4f|%.4f|%.3f|%.3f|%d\n", set, name.c_str(), double(amount), s.noteF1, s.durationF1, s.frag, s.shortShare, s.pieces);
    }
}

// accuracy dump <old|techno> <index> <amount>: truth and detected notes of one mix (for debugging)
int runDump(const char* set, int index, float amount) {
    std::vector<tmetric::Note> truth;
    std::vector<float> audio;
    double bpm = ttr::kDefaultBpm;
    if (std::strcmp(set, "old") == 0) {
        const std::vector<oldmix::Mix> mixes = oldmix::buildMixes();
        for (const oldmix::Truth& t : mixes[size_t(index)].truth) truth.push_back({t.pitch, t.start, t.end});
        audio = mixes[size_t(index)].audio;
    } else {
        const ttech::Mix m = ttech::build(ttech::recipes()[size_t(index)]);
        for (const ttech::Truth& t : m.truth) truth.push_back({t.pitch, t.start, t.end});
        audio = m.audio;
        bpm = m.bpm;
    }
    for (const tmetric::Note& t : truth) std::printf("truth|%d|%.3f|%.3f\n", t.pitch, t.start, t.end);
    for (const NoteEvent& n : grab(audio, oldmix::kSampleRate, bpm, amount)) std::printf("found|%d|%.3f|%.3f\n", n.pitch, n.startSec, n.endSec);
    return 0;
}

int runMixes() {
    for (const oldmix::Mix& m : oldmix::buildMixes()) {
        std::vector<tmetric::Note> truth;
        for (const oldmix::Truth& t : m.truth) truth.push_back({t.pitch, t.start, t.end});
        report("old", m.name, truth, m.audio, ttr::kDefaultBpm);
    }
    for (const ttech::Recipe& r : ttech::recipes()) {
        const ttech::Mix m = ttech::build(r);
        std::vector<tmetric::Note> truth;
        for (const ttech::Truth& t : m.truth) truth.push_back({t.pitch, t.start, t.end});
        report("techno", m.name, truth, m.audio, m.bpm);
    }
    return 0;
}

// ---- reference window -------------------------------------------------------------------------------------------
// Rows read off the reference result (spec 4.4, section A): pitch (+-1 semitone) and time ranges as fractions of the window.
struct RefRow { const char* label; int pitch; std::vector<std::pair<double, double>> ranges; };

const std::vector<RefRow>& refRows() {
    static const std::vector<RefRow> rows = {
        {"F1", 29, {{0.00, 1.00}}},
        {"F2", 41, {{0.12, 0.28}, {0.31, 1.00}}},
        {"C2", 36, {{0.18, 0.22}, {0.23, 0.28}, {0.29, 0.33}, {0.34, 0.37}, {0.67, 0.72}, {0.74, 0.78}}},
        {"A#1", 34, {{0.54, 0.63}}},
        {"B1", 35, {{0.85, 0.88}}},
        {"C3", 48, {{0.40, 0.42}, {0.65, 0.67}, {0.77, 0.84}}},
        {"F3", 53, {{0.09, 0.11}, {0.14, 0.16}, {0.50, 0.52}, {0.54, 0.56}, {0.91, 0.96}}},
        {"G3/G#3", 55, {{0.30, 0.31}, {0.36, 0.38}, {0.70, 0.72}}}};
    return rows;
}

struct RowResult { double coverage = 0.0; int pieces = 0; };

RowResult rowResult(const RefRow& row, const std::vector<NoteEvent>& notes, double window) {
    RowResult r;
    double total = 0.0, covered = 0.0;
    for (const auto& range : row.ranges) {
        const double a = range.first * window, b = range.second * window;
        total += b - a;
        // union of the pieces within +-1 semitone, clipped to the range
        std::vector<std::pair<double, double>> parts;
        for (const NoteEvent& n : notes)
            if (std::abs(n.pitch - row.pitch) <= 1 && tmetric::overlap(n.startSec, n.endSec, a, b) > 0.0) parts.push_back({std::max(a, n.startSec), std::min(b, n.endSec)});
        std::sort(parts.begin(), parts.end());
        double end = a;
        for (const auto& p : parts) { covered += std::max(0.0, p.second - std::max(p.first, end)); end = std::max(end, p.second); }
    }
    r.coverage = total > 0.0 ? covered / total : 0.0;
    for (const NoteEvent& n : notes) {
        if (std::abs(n.pitch - row.pitch) > 1) continue;
        for (const auto& range : row.ranges)
            if (tmetric::overlap(n.startSec, n.endSec, range.first * window, range.second * window) > 0.0) { ++r.pieces; break; }
    }
    return r;
}

// Pieces that belong to none of the rows (pitch +-1 and a time range, tolerance 0.5 s around the range).
int falsePositives(const std::vector<NoteEvent>& notes, double window) {
    int fp = 0;
    for (const NoteEvent& n : notes) {
        bool explained = false;
        for (const RefRow& row : refRows())
            for (const auto& range : row.ranges)
                if (std::abs(n.pitch - row.pitch) <= 1 && tmetric::overlap(n.startSec, n.endSec, range.first * window - 0.15, range.second * window + 0.15) > 0.0) explained = true;
        fp += explained ? 0 : 1;
    }
    return fp;
}

double windowScore(const std::vector<NoteEvent>& notes, double window) {
    double recall = 0.0;
    for (const RefRow& row : refRows()) recall += rowResult(row, notes, window).coverage;
    recall /= double(refRows().size());
    const int fp = falsePositives(notes, window);
    const double precision = notes.empty() ? 0.0 : 1.0 - double(fp) / double(notes.size());
    return tmetric::f1Of(precision, recall);
}

// BPM and the time of a beat from the low-frequency envelope (one-pole 150 Hz, onset = positive difference), folded over one period.
void estimateTempo(const std::vector<float>& mono, double rate, double* bpm, double* phaseSec) {
    const int step = std::max(1, int(rate / 200.0));
    std::vector<float> env;
    float lp = 0.0f, smooth = 0.0f;
    const float a = float(1.0 - std::exp(-2.0 * M_PI * 150.0 / rate));
    for (size_t i = 0; i < mono.size(); i += size_t(step)) {
        double e = 0.0;
        for (int k = 0; k < step && i + size_t(k) < mono.size(); ++k) { lp += a * (mono[i + size_t(k)] - lp); e += double(lp) * lp; }
        const float v = std::sqrt(float(e / step));
        env.push_back(std::max(0.0f, v - smooth));
        smooth = v;
    }
    const double envRate = rate / step;
    double bestScore = -1.0;
    for (double b = 100.0; b <= 150.0; b += 0.02) {
        const double period = envRate * 60.0 / b;
        const int bins = int(period);
        std::vector<double> fold(size_t(bins), 0.0);
        for (size_t i = 0; i < env.size(); ++i) fold[size_t(std::fmod(double(i), period) / period * bins) % size_t(bins)] += double(env[i]);
        const auto peak = std::max_element(fold.begin(), fold.end());
        if (*peak > bestScore) { bestScore = *peak; *bpm = b; *phaseSec = double(peak - fold.begin()) / bins * period / envRate; }
    }
}

void printRows(const std::vector<NoteEvent>& notes, double window);

int runTrack(const char* path, double startSec, double beats, double bpm) {
    std::vector<float> audio;
    const double rate = twav::readWav(path, startSec, beats * 60.0 / (bpm > 0 ? bpm : 130.0), &audio);
    if (rate <= 0.0) { std::fprintf(stderr, "cannot read %s\n", path); return 1; }
    const double window = double(audio.size()) / rate;
    const std::vector<NoteEvent> notes = grab(audio, rate, bpm > 0 ? bpm : 130.0, 1.0f);
    std::printf("window start %.2f s, %.0f beats at %.2f BPM (%.2f s), %zu pieces\n", startSec, beats, bpm, window, notes.size());
    printRows(notes, window);
    return 0;
}

void printRows(const std::vector<NoteEvent>& notes, double window) {
    int found = 0;
    for (const RefRow& row : refRows()) {
        const RowResult r = rowResult(row, notes, window);
        found += r.coverage >= 0.5 ? 1 : 0;
        std::printf("row|%s|%s|%.0f|%d\n", row.label, r.coverage >= 0.5 ? "yes" : "no", r.coverage * 100.0, r.pieces);
    }
    std::printf("found|%d|fp|%d\n", found, falsePositives(notes, window));
    for (const NoteEvent& n : notes) std::printf("note|%d|%.2f|%.2f\n", n.pitch, n.startSec, n.endSec);
}

// accuracy matrix <wav> <startSec> <beats> <bpm> <out>: raw note energies in dB (rows = notes, high to low) and the notes, for a picture
int runMatrix(const char* path, double startSec, double beats, double bpm, const char* outPath) {
    std::vector<float> audio;
    const double rate = twav::readWav(path, startSec, beats * 60.0 / bpm, &audio);
    if (rate <= 0.0) return 1;
    const std::shared_ptr<Spectrogram> spec = computeSpectrogram(audio.data(), audio.size(), rate);
    if (!spec) return 1;
    std::FILE* out = std::fopen(outPath, "w");
    if (!out) return 1;
    std::fprintf(out, "%d %d %.6f\n", spec->frames, kNumNotes, spec->hopSec);
    for (int n = kNumNotes - 1; n >= 0; --n) {
        for (int f = 0; f < spec->frames; ++f) std::fprintf(out, "%.1f ", 20.0 * std::log10(double(spec->rawAt(f, n)) / double(spec->peak) + 1e-6));
        std::fprintf(out, "\n");
    }
    for (const NoteEvent& n : grab(audio, rate, bpm, 1.0f)) std::fprintf(out, "note %d %.3f %.3f\n", n.pitch, n.startSec, n.endSec);
    std::fclose(out);
    return 0;
}

int runScan(const char* path, double bpm) {
    std::vector<float> all;
    const double rate = twav::readWav(path, 0.0, 1e9, &all);
    if (rate <= 0.0) { std::fprintf(stderr, "cannot read %s\n", path); return 1; }
    double phase = 0.0, est = 130.0;
    estimateTempo(all, rate, &est, &phase);
    if (bpm <= 0.0) bpm = est;
    const double beat = 60.0 / bpm;
    std::printf("tempo %.2f BPM (estimated %.2f), first beat at %.3f s\n", bpm, est, phase);
    constexpr double kBeats = 20.0;
    struct Hit { double start, score; };
    std::vector<Hit> hits;
    for (double t = phase; t + kBeats * beat < double(all.size()) / rate; t += beat) {
        const size_t a = size_t(t * rate), n = size_t(kBeats * beat * rate);
        const std::vector<float> slice(all.begin() + long(a), all.begin() + long(a + n));
        hits.push_back({t, windowScore(grab(slice, rate, bpm, 1.0f), double(n) / rate)});
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& x, const Hit& y) { return x.score > y.score; });
    for (size_t i = 0; i < std::min<size_t>(6, hits.size()); ++i) std::printf("scan|%.3f|%.3f\n", hits[i].start, hits[i].score);
    return 0;
}

// ---- external engines (reference runs outside the plugin) ------------------------------------------------------
struct NamedMix { std::string id; double bpm; std::vector<float> audio; std::vector<tmetric::Note> truth; };

std::vector<NamedMix> allMixes() {
    std::vector<NamedMix> out;
    for (const oldmix::Mix& m : oldmix::buildMixes()) {
        NamedMix n{"old-" + m.name, ttr::kDefaultBpm, m.audio, {}};
        for (const oldmix::Truth& t : m.truth) n.truth.push_back({t.pitch, t.start, t.end});
        out.push_back(std::move(n));
    }
    for (const ttech::Recipe& r : ttech::recipes()) {
        const ttech::Mix m = ttech::build(r);
        NamedMix n{m.name, m.bpm, m.audio, {}};
        for (const ttech::Truth& t : m.truth) n.truth.push_back({t.pitch, t.start, t.end});
        out.push_back(std::move(n));
    }
    return out;
}

bool writeWavFloat(const std::string& path, const std::vector<float>& audio, double rate) {
    std::ofstream out(path, std::ios::binary);
    if (!out) return false;
    auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
    auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };
    const uint32_t bytes = uint32_t(audio.size() * 4);
    out.write("RIFF", 4); u32(36 + bytes); out.write("WAVEfmt ", 8); u32(16); u16(3); u16(1);
    u32(uint32_t(rate)); u32(uint32_t(rate) * 4); u16(4); u16(32); out.write("data", 4); u32(bytes);
    out.write(reinterpret_cast<const char*>(audio.data()), std::streamsize(bytes));
    return bool(out);
}

std::vector<NoteEvent> readNotes(const std::string& path, bool* ok) {
    std::vector<NoteEvent> notes;
    std::ifstream in(path);
    *ok = bool(in);
    NoteEvent n;
    char sep1 = 0, sep2 = 0;
    while (in >> n.pitch >> sep1 >> n.startSec >> sep2 >> n.endSec)
        if (sep1 == '|' && sep2 == '|' && n.endSec > n.startSec) notes.push_back(n);
    return notes;
}

int runExport(const char* dir) {
    for (const NamedMix& m : allMixes()) {
        const std::string base = std::string(dir) + "/" + m.id;
        if (!writeWavFloat(base + ".wav", m.audio, oldmix::kSampleRate)) { std::fprintf(stderr, "cannot write %s.wav\n", base.c_str()); return 1; }
        std::ofstream(base + ".bpm") << m.bpm << "\n";
    }
    return 0;
}

int runScoreMixes(const char* dir) {
    for (const NamedMix& m : allMixes()) {
        bool ok = false;
        const std::vector<NoteEvent> notes = readNotes(std::string(dir) + "/" + m.id + ".notes", &ok);
        if (!ok) { std::fprintf(stderr, "missing notes for %s\n", m.id.c_str()); return 1; }
        const tmetric::Score s = tmetric::score(m.truth, toNotes(notes));
        std::printf("%s|%.4f|%.4f|%.3f|%.3f|%d\n", m.id.c_str(), s.noteF1, s.durationF1, s.frag, s.shortShare, s.pieces);
    }
    return 0;
}

int runScoreTrack(const char* path, double window) {
    bool ok = false;
    const std::vector<NoteEvent> notes = readNotes(path, &ok);
    if (!ok) { std::fprintf(stderr, "cannot read %s\n", path); return 1; }
    std::printf("external notes %s, window %.2f s, %zu pieces\n", path, window, notes.size());
    printRows(notes, window);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc >= 5 && std::strcmp(argv[1], "track") == 0) return runTrack(argv[2], std::atof(argv[3]), std::atof(argv[4]), argc > 5 ? std::atof(argv[5]) : 0.0);
    if (argc >= 5 && std::strcmp(argv[1], "dump") == 0) return runDump(argv[2], std::atoi(argv[3]), float(std::atof(argv[4])));
    if (argc >= 7 && std::strcmp(argv[1], "matrix") == 0) return runMatrix(argv[2], std::atof(argv[3]), std::atof(argv[4]), std::atof(argv[5]), argv[6]);
    if (argc >= 3 && std::strcmp(argv[1], "scan") == 0) return runScan(argv[2], argc > 3 ? std::atof(argv[3]) : 0.0);
    if (argc >= 3 && std::strcmp(argv[1], "export") == 0) return runExport(argv[2]);
    if (argc >= 3 && std::strcmp(argv[1], "score-mixes") == 0) return runScoreMixes(argv[2]);
    if (argc >= 4 && std::strcmp(argv[1], "score-track") == 0) return runScoreTrack(argv[2], std::atof(argv[3]));
    return runMixes();
}
