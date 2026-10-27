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

#include "TestDrums.h"
#include "TestSignals.h"
#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"

using namespace ttr;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr double kOnsetTolSec = 0.050;
constexpr double kShortNoteSec = 0.120;

struct Truth { int pitch; double start, end; };
struct Mix { std::string name; std::vector<float> audio; std::vector<Truth> truth; };

// Small deterministic generator, independent of the platform's rand().
struct Lcg {
    uint32_t state = 2463534242u;
    double next() { state = state * 1664525u + 1013904223u; return double(state >> 8) / double(1u << 24); }
};

void addInto(std::vector<float>& dst, const std::vector<float>& src, float gain = 1.0f) {
    for (size_t i = 0; i < dst.size() && i < src.size(); ++i) dst[i] += gain * src[i];
}

std::vector<float> renderTruth(const std::vector<Truth>& notes, double centsOff, double length, bool saw, float amp, double vibratoCents) {
    std::vector<tsig::HzTone> tones;
    for (const Truth& n : notes)
        tones.push_back({tsig::midiToHz(n.pitch) * std::pow(2.0, centsOff / 1200.0), n.start, n.end, amp, saw, vibratoCents, 5.0, 0.0});
    return tsig::synthHz(tones, kSampleRate, length);
}

std::vector<Truth> melody(Lcg& rng, double length, int lo, int hi, double minLen, double maxLen) {
    std::vector<Truth> notes;
    int pitch = (lo + hi) / 2;
    for (double t = 0.2; t < length - maxLen - 0.2;) {
        const double len = minLen + (maxLen - minLen) * rng.next();
        pitch = std::clamp(pitch + int(rng.next() * 9.0) - 4, lo, hi);
        notes.push_back({pitch, t, t + len});
        t += len + 0.05 + 0.1 * rng.next();
    }
    return notes;
}

std::vector<Truth> chords(double length) {
    const int roots[] = {57, 53, 48, 55};
    std::vector<Truth> notes;
    int k = 0;
    for (double t = 0.2; t < length - 2.0; t += 2.0, ++k)
        for (int offset : {0, 4, 7}) notes.push_back({roots[k % 4] + offset, t, t + 1.8});
    return notes;
}

std::vector<float> drumTrack(double length) {
    return tdrum::render(tdrum::pattern(120.0, length), kSampleRate, length, 0.8f);
}

void addNoise(std::vector<float>& audio, float level) {
    tdrum::Noise noise;
    for (float& v : audio) v += level * noise.next();
}

std::vector<Mix> buildMixes() {
    constexpr double kLen = 14.0;
    std::vector<Mix> mixes;
    auto make = [&](const char* name, const std::vector<Truth>& truth, double cents, float amp, double vibrato) {
        Mix m;
        m.name = name;
        m.truth = truth;
        m.audio = renderTruth(truth, cents, kLen, true, amp, vibrato);
        return m;
    };
    Lcg rng;
    const std::vector<Truth> lead = melody(rng, kLen, 60, 76, 0.25, 0.6);
    const std::vector<Truth> bass = melody(rng, kLen, 36, 47, 0.4, 0.9);
    mixes.push_back(make("melody", lead, 0.0, 0.4f, 0.0));
    mixes.push_back(make("chords", chords(kLen), 0.0, 0.25f, 0.0));
    mixes.push_back(make("bass", bass, 0.0, 0.5f, 0.0));
    mixes.push_back(make("vibrato", lead, 0.0, 0.4f, 35.0));
    mixes.push_back(make("detune-30ct", lead, -30.0, 0.4f, 0.0));
    mixes.push_back(make("detune+30ct", lead, 30.0, 0.4f, 0.0));
    Mix noisy = make("noise", lead, 0.0, 0.4f, 0.0);
    addNoise(noisy.audio, 0.05f);
    mixes.push_back(noisy);
    Mix drums = make("drums+melody", lead, 0.0, 0.35f, 0.0);
    addInto(drums.audio, drumTrack(kLen));
    mixes.push_back(drums);
    Mix full = make("full mix", lead, 0.0, 0.3f, 0.0);
    std::vector<Truth> all = lead;
    all.insert(all.end(), bass.begin(), bass.end());
    const std::vector<Truth> pads = chords(kLen);
    all.insert(all.end(), pads.begin(), pads.end());
    full.audio = renderTruth(all, 0.0, kLen, true, 0.22f, 0.0);
    full.truth = all;
    addInto(full.audio, drumTrack(kLen));
    addNoise(full.audio, 0.02f);
    mixes.push_back(full);
    return mixes;
}

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

// First `seconds` of a WAV file as mono floats; returns the sample rate (0 on failure). PCM 16/24/32 bit and float32.
double readWav(const char* path, double seconds, std::vector<float>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0.0;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0) return 0.0;
    auto u16 = [&](size_t p) { return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8; };
    auto u32 = [&](size_t p) { return u16(p) | u16(p + 2) << 16; };
    uint32_t format = 0, channels = 0, rate = 0, bits = 0;
    size_t dataPos = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= b.size();) {
        const size_t len = u32(p + 4);
        if (std::memcmp(&b[p], "fmt ", 4) == 0) { format = u16(p + 8); channels = u16(p + 10); rate = u32(p + 12); bits = u16(p + 22); }
        if (std::memcmp(&b[p], "data", 4) == 0) { dataPos = p + 8; dataLen = std::min(len, b.size() - dataPos); break; }
        p += 8 + len + (len & 1);
    }
    if (!channels || !rate || !dataPos || (bits != 16 && bits != 24 && bits != 32)) return 0.0;
    const size_t bytes = bits / 8, frames = std::min(dataLen / (bytes * channels), size_t(seconds * rate));
    out->assign(frames, 0.0f);
    for (size_t f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (uint32_t c = 0; c < channels; ++c) {
            const size_t p = dataPos + (f * channels + c) * bytes;
            if (bits == 16) sum += double(int16_t(u16(p))) / 32768.0;
            else if (bits == 24) sum += double(int32_t((uint32_t(b[p]) << 8 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 24)) >> 8) / 8388608.0;
            else if (format == 3) { float v; std::memcpy(&v, &b[p], 4); sum += v; }
            else sum += double(int32_t(u32(p))) / 2147483648.0;
        }
        (*out)[f] = float(sum / channels);
    }
    return double(rate);
}

int runFile(const char* path, double seconds) {
    std::vector<float> audio;
    const double rate = readWav(path, seconds, &audio);
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
