// The transcription with the real model on real music: muscriptor.cpp's own 15 s test excerpt (a rock band, CC BY 4.0,
// fetched into the build tree, never committed), loaded as a song file the way the editor does it (Core Audio -> kernel ->
// MuScriptor). Checks that the notes arrive chunk by chunk while the song already plays, that the plug-in path finds the
// same notes as calling the library directly, speed, silence, a missing model, and one MIDI track per instrument.
// Prints MSTEST PASSED / MSTEST FAILED / MSTEST SKIPPED (no model on this machine or Intel; run by build.sh).
// Model: argv[1], else ~/Library/Tonetrace/models/muscriptor-medium-f16.gguf (downloaded by the plug-in on first use).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "TestKernel.h"
#include "analysis/ml/AudioFileDecoder.h"
#include "analysis/ml/MuScriptorEngine.h"
#include "analysis/ml/MuScriptorTranscriber.h"
#include "analysis/ml/muscriptor/MuScriptorBridge.h"
#include "core/Instruments.h"
#include "midi/ExportPlanner.h"

using namespace ttr;

namespace {

constexpr double kOnsetTolerance = 0.03;  // seconds
constexpr double kMinMatchShare = 0.9;     // plug-in path vs direct call
constexpr double kMaxRealtimeFactor = 1.0;
int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    failures += ok ? 0 : 1;
}

// 16 kHz mono float32 WAV (the fixture's format). Empty on any mismatch.
std::vector<float> readFixture(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    const std::vector<char> b((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp(b.data(), "RIFF", 4) != 0) return {};
    for (size_t pos = 12; pos + 8 <= b.size();) {
        uint32_t size = 0;
        std::memcpy(&size, b.data() + pos + 4, 4);
        if (std::memcmp(b.data() + pos, "data", 4) == 0) {
            std::vector<float> out(std::min<size_t>(size, b.size() - pos - 8) / sizeof(float));
            std::memcpy(out.data(), b.data() + pos + 8, out.size() * sizeof(float));
            return out;
        }
        pos += 8 + size + (size & 1u);
    }
    return {};
}

void songTest(const std::string& model, const std::string& fixture) {
    const std::vector<float> audio16k = readFixture(fixture);
    check(audio16k.size() > size_t(10.0 * kMuScriptorRate), "fixture audio read");
    if (audio16k.empty()) return;
    std::string why;
    std::shared_ptr<msb::Model> direct = msb::load(model, &why);
    std::vector<msb::Note> reference;
    check(direct && msb::transcribe(*direct, audio16k.data(), audio16k.size(), &reference, {}, {}, &why), "direct library call runs");
    direct.reset();

    TonetraceKernel k;
    k.prepare(tk::kRate, tk::kBlock);
    k.setHostTransport(120.0, 0.0);
    const auto t0 = std::chrono::steady_clock::now();
    check(k.loadSong(fixture), "the fixture loads as a song");
    check(tk::waitFor([&] { return k.document() && k.document()->audio; }, 10000), "the song is decoded and playable before the model is done");
    k.play();
    std::set<size_t> noteCounts;  // how many notes the document held at each look: grows step by step
    double heardSec = 0.0;
    while (k.isWorking()) {
        tk::run(k, tk::silence(8));  // the host keeps playing meanwhile
        const DocumentPtr doc = k.document();
        if (doc && doc->transcription.isActive() && !doc->notes.empty()) noteCounts.insert(doc->notes.size());
        heardSec = std::max(heardSec, k.positionSec());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    const DocumentPtr doc = k.document();
    check(doc && doc->transcription.phase == TranscriptionPhase::Done, "the transcription finishes");
    if (!doc) return;
    std::printf("     %.1f s song transcribed in %.1f s (RTF %.2f, incl. model load), %zu notes; direct call %zu notes\n", doc->lengthSec, secs,
                secs / doc->lengthSec, doc->notes.size(), reference.size());
    std::printf("     notes seen while it ran:");
    for (size_t c : noteCounts) std::printf(" %zu", c);
    std::printf("; playback reached %.1f s meanwhile\n", heardSec);
    // 15 s are three 5 s chunks and the model reports a chunk's notes one chunk late: one partial state is all there is.
    check(!noteCounts.empty() && *noteCounts.begin() < doc->notes.size(), "the notes arrive step by step (a partial state shows before the end)");
    check(heardSec > 0.5, "the song played while the model was working");
    const std::vector<InstrumentSummary> instruments = instrumentsIn(doc->notes);
    std::printf("     instruments:");
    for (const InstrumentSummary& s : instruments) std::printf(" %s(%d)", instrumentName(s.program).c_str(), s.noteCount);
    std::printf("\n");
    const bool hasDrums = std::any_of(instruments.begin(), instruments.end(), [](const InstrumentSummary& s) { return s.program == kDrumProgram; });
    check(instruments.size() >= 3 && hasDrums, "several instruments incl. drums");
    int tonal = 0, matched = 0;
    for (const msb::Note& r : reference) {
        if (r.isDrum || r.pitch < kMinNote || r.pitch > kMaxNote) continue;
        ++tonal;
        for (const NoteEvent& n : doc->notes)
            if (n.pitch == r.pitch && n.program == r.program && std::abs(n.startSec - r.onset) < kOnsetTolerance) { ++matched; break; }
    }
    const double share = tonal > 0 ? double(matched) / tonal : 0.0;
    std::printf("     %d of %d tonal reference notes found through the plug-in path (%.0f %%)\n", matched, tonal, share * 100.0);
    check(tonal >= 20 && share >= kMinMatchShare, "plug-in path matches the direct call (>= 90 % of tonal notes, same instrument)");
    check(secs / doc->lengthSec < kMaxRealtimeFactor, "faster than real time");
    const ParsedMidi midi = parseMidiFile(k.buildMidi());
    int onDrumChannel = 0;
    for (const MidiNote& m : midi.notes) onDrumChannel += m.channel == kDrumChannel ? 1 : 0;
    check(midi.valid && midi.tracks == int(instruments.size()) + 1 && midi.notes.size() == doc->notes.size() && onDrumChannel > 0,
          "one MIDI track per instrument, drums on channel 10, every note exported");
    k.pause();
    k.deactivate();
}

void silenceTest() {
    std::string why;
    std::vector<NoteEvent> notes;
    const bool ok = transcribeSong(std::vector<float>(size_t(3.0 * kMuScriptorRate), 0.0f), 120.0, nullptr,
                                   [&](const std::vector<NoteEvent>& n, double, float) { notes = n; }, &why);
    check(ok && notes.empty(), "silence gives no notes");
}

}  // namespace

int main(int argc, char** argv) {
    if (!msb::isSupported()) { std::printf("MSTEST SKIPPED (MuScriptor needs Apple Silicon)\n"); return 0; }
    installAudioFileDecoder();
    setenv(kMuScriptorModelEnv, "/nonexistent/muscriptor.gguf", 1);
    installMuScriptorLoader();
    std::string why;
    check(!noteTranscriber(&why) && why.find("model not found") != std::string::npos, "missing model file gives a reason");
    const std::string model = argc > 1 && *argv[1] ? argv[1] : (unsetenv(kMuScriptorModelEnv), muscriptorModelPath());
    if (FILE* f = std::fopen(model.c_str(), "rb")) {
        std::fclose(f);
    } else {
        std::printf("MSTEST SKIPPED (no model at %s)\n", model.c_str());
        return failures == 0 ? 0 : 1;
    }
    setenv(kMuScriptorModelEnv, model.c_str(), 1);  // the loader is asked again on the next song and finds it now
    songTest(model, argc > 2 ? argv[2] : TONETRACE_MS_FIXTURE);
    silenceTest();
    std::printf("%s\n", failures == 0 ? "MSTEST PASSED" : "MSTEST FAILED");
    return failures == 0 ? 0 : 1;
}
