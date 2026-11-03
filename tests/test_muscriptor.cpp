// 4.6: MuScriptor engine pieces that run without the model: note conversion (drums), registry retries, fallback, editor state.
#include <cmath>
#include <memory>

#include "TestUtil.h"
#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"
#include "analysis/ml/MuScriptorEngine.h"
#include "state/UiSettings.h"

using namespace ttr;

namespace {

// Returns fixed notes and remembers what it got.
class FakeTranscriber : public NoteTranscriber {
public:
    explicit FakeTranscriber(std::vector<TranscribedNote> notes, bool isOk = true) : notes_(std::move(notes)), isOk_(isOk) {}
    bool transcribe(const std::vector<float>& audio, std::vector<TranscribedNote>* out, TaskControl*, std::string* reason) override {
        lastSamples = audio.size();
        if (!isOk_) { *reason = "the model failed"; return false; }
        *out = notes_;
        return true;
    }
    size_t lastSamples = 0;

private:
    std::vector<TranscribedNote> notes_;
    bool isOk_;
};

Snapshot sineSnapshot(double seconds) {
    constexpr double kRate = 44100.0;
    std::vector<float> audio(size_t(kRate * seconds));
    for (size_t i = 0; i < audio.size(); ++i) audio[i] = 0.5f * float(std::sin(2.0 * M_PI * 220.0 * double(i) / kRate));
    Snapshot snap;
    snap.spectrogram = computeSpectrogram(audio.data(), audio.size(), kRate);
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = kRate;
    for (float v : audio) clip->samples.push_back(int16_t(std::lround(v * 32767.0f)));
    snap.audio = clip;
    snap.bpm = 120.0;
    return snap;
}

}  // namespace

TEST(muscriptor_notes_lengthen_drums_and_keep_tonal_notes) {
    const std::vector<TranscribedNote> raw = {
        {1.00, 1.50, 45, false},  // bass note: kept as is
        {0.50, 0.51, 36, true},   // kick: 1/16 at 120 BPM = 0.125 s
        {0.55, 0.56, 42, true},   // hi-hat, next hi-hat after 0.05 s -> capped there
        {0.60, 0.61, 42, true},
        {0.20, 0.40, 10, false},  // below the note map: dropped
    };
    const std::vector<NoteEvent> notes = muscriptorNotes(raw, 120.0);
    CHECK(notes.size() == 4);
    CHECK(notes[0].pitch == 36 && std::abs(notes[0].endSec - 0.625) < 1e-9);
    CHECK(notes[1].pitch == 42 && std::abs(notes[1].endSec - 0.60) < 1e-9);
    CHECK(notes[2].pitch == 42 && std::abs(notes[2].endSec - 0.725) < 1e-9);
    CHECK(notes[3].pitch == 45 && notes[3].startSec == 1.00 && notes[3].endSec == 1.50);
    for (const NoteEvent& n : notes) CHECK(!n.selected && n.endSec > n.startSec);
}

TEST(muscriptor_drum_length_survives_a_missing_tempo) {
    const std::vector<NoteEvent> notes = muscriptorNotes({{0.0, 0.01, 38, true}}, 0.0);
    CHECK(notes.size() == 1 && std::abs(notes[0].endSec - 0.125) < 1e-9);  // falls back to 120 BPM
}

TEST(muscriptor_grab_resamples_to_16k_and_reports_its_engine) {
    const Snapshot snap = sineSnapshot(2.0);
    auto fake = std::make_shared<FakeTranscriber>(std::vector<TranscribedNote>{{0.1, 0.9, 57, false}});
    setTranscriber(fake);
    const GrabOutcome out = quickGrabWithEngine(snap, 0.5f, FilterSettings{}, 0.0, GrabEngine::MuScriptor);
    CHECK(out.used == GrabEngine::MuScriptor && out.fallbackReason.empty());
    CHECK(out.notes.size() == 1 && out.notes[0].pitch == 57);
    CHECK(std::abs(double(fake->lastSamples) - 2.0 * kMuScriptorRate) <= 2.0);
    setTranscriber(nullptr);
}

TEST(muscriptor_falls_back_to_classic_with_the_reason) {
    const Snapshot snap = sineSnapshot(2.0);
    const size_t classic = quickGrabSnapshot(snap, 0.5f, FilterSettings{}, 0.0).size();
    setTranscriberLoader([](std::string* reason) { *reason = "MuScriptor model downloading (35 % of 620 MB)"; return std::shared_ptr<NoteTranscriber>(); });
    GrabOutcome out = quickGrabWithEngine(snap, 0.5f, FilterSettings{}, 0.0, GrabEngine::MuScriptor);
    CHECK(out.used == GrabEngine::Classic && out.fallbackReason.find("downloading") != std::string::npos);
    CHECK(out.notes.size() == classic);
    CHECK(transcriberStatus().find("downloading") != std::string::npos);
    setTranscriber(std::make_shared<FakeTranscriber>(std::vector<TranscribedNote>{}, false));
    out = quickGrabWithEngine(snap, 0.5f, FilterSettings{}, 0.0, GrabEngine::MuScriptor);
    CHECK(out.used == GrabEngine::Classic && out.fallbackReason == "the model failed");
    Snapshot noAudio = snap;
    noAudio.audio = nullptr;
    out = quickGrabWithEngine(noAudio, 0.5f, FilterSettings{}, 0.0, GrabEngine::MuScriptor);
    CHECK(out.used == GrabEngine::Classic && out.fallbackReason.find("not stored") != std::string::npos);
    setTranscriberLoader(nullptr);
}

TEST(muscriptor_loader_is_asked_again_until_the_model_is_there) {
    int calls = 0;
    setTranscriberLoader([&calls](std::string* reason) -> std::shared_ptr<NoteTranscriber> {
        if (++calls == 1) { *reason = "MuScriptor model not downloaded yet"; return nullptr; }
        return std::make_shared<FakeTranscriber>(std::vector<TranscribedNote>{});
    });
    std::string why;
    CHECK(!noteTranscriber(&why) && why == "MuScriptor model not downloaded yet");
    CHECK(noteTranscriber() != nullptr && transcriberStatus() == "ready");
    CHECK(noteTranscriber() != nullptr && calls == 2);  // loaded once, then kept
    setTranscriberLoader(nullptr);
}

TEST(muscriptor_prefetch_runs_only_without_a_model) {
    int prefetches = 0;
    setTranscriberLoader(nullptr);
    setTranscriberPrefetch([&prefetches] { ++prefetches; return std::string("downloading"); });
    CHECK(prefetchTranscriber() == "downloading");
    CHECK(prefetches == 1);
    setTranscriber(std::make_shared<FakeTranscriber>(std::vector<TranscribedNote>{}));
    CHECK(prefetchTranscriber().empty());
    CHECK(prefetches == 1);
    setTranscriberPrefetch(nullptr);
    setTranscriber(nullptr);
}

TEST(editor_state_keeps_muscriptor_and_rejects_unknown_engines) {
    UiSettings s;
    s.grabEngine = int(GrabEngine::MuScriptor);
    std::vector<uint8_t> bytes = encodeUiSettings(s);
    UiSettings back;
    CHECK(decodeUiSettings(bytes.data(), bytes.size(), &back) && back.grabEngine == int(GrabEngine::MuScriptor));
    s.grabEngine = 7;
    bytes = encodeUiSettings(s);
    CHECK(decodeUiSettings(bytes.data(), bytes.size(), &back) && back.grabEngine == int(GrabEngine::Classic));
    CHECK(grabEngineFromIndex(2) == GrabEngine::MuScriptor && grabEngineFromIndex(-1) == GrabEngine::Classic && grabEngineFromIndex(3) == GrabEngine::Classic);
    CHECK(std::string(grabEngineName(GrabEngine::MuScriptor)) == "MuScriptor");
}
