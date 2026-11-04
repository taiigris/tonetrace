// State v6: round trip through the kernel (song path, notes with instruments, mixer, export tempo, recording audio),
// refusal of Tonetrace 4 states, robustness against damaged data, and the editor settings.
#include <cmath>
#include <cstring>
#include <random>

#include "TestKernel.h"
#include "TestUtil.h"
#include "core/Instruments.h"
#include "state/Crc32.h"
#include "state/StateCodec.h"
#include "state/UiSettings.h"

using namespace ttr;

namespace {

std::shared_ptr<Document> sampleDocument() {
    auto doc = std::make_shared<Document>();
    doc->sourceName = "Song.wav";
    doc->sourcePath = "/tmp/Song.wav";
    doc->lengthSec = 3.0;
    doc->transcription.phase = TranscriptionPhase::Done;
    doc->notes = {{45, 0.1, 1.9, 33}, {36, 1.0, 1.1, kDrumProgram}, {72, 2.0, 2.8, 0}};
    return doc;
}

}  // namespace

TEST(state_round_trip_keeps_notes_instruments_mix_and_tempo) {
    MixerState mix = MixerState{}.with(kDrumProgram, {true, false, 0.5f}).with(33, {false, true, 0.9f});
    ParamValues params = defaultParams();
    params[kParamMix] = 0.25f;
    params[kParamRecord] = 1.0f;
    const auto doc = sampleDocument();
    const std::vector<uint8_t> bytes = encodeState(params, doc.get(), mix, 97.0);
    DecodedState out;
    std::string reason;
    CHECK_MSG(decodeState(bytes.data(), bytes.size(), &out, &reason), "%s", reason.c_str());
    CHECK(out.formatVersion == kStateFormatVersion && out.params[kParamMix] == 0.25f && out.params[kParamRecord] == 0.0f);
    CHECK(out.exportBpm == 97.0 && out.mixer == mix);
    CHECK(out.document && out.document->sourcePath == "/tmp/Song.wav" && out.document->notes.size() == 3 && instrumentsIn(out.document->notes).size() == 3);
    CHECK(out.document && out.document->transcription.phase == TranscriptionPhase::Done && !out.document->audio);
    ParamValues onlyParams;
    CHECK(decodeStateParams(bytes.data(), bytes.size(), &onlyParams) && onlyParams[kParamMix] == 0.25f);
}

TEST(a_running_transcription_is_saved_as_cancelled_with_its_notes) {
    auto doc = sampleDocument();
    doc->transcription.phase = TranscriptionPhase::Running;
    const std::vector<uint8_t> bytes = encodeState(defaultParams(), doc.get(), MixerState{}, 0.0);
    DecodedState out;
    CHECK(decodeState(bytes.data(), bytes.size(), &out) && out.document && out.document->transcription.phase == TranscriptionPhase::Cancelled);
}

TEST(the_kernel_restores_a_song_and_reloads_its_audio_from_the_file) {
    tk::installFakeDecoder(3.0);
    std::vector<uint8_t> saved;
    {
        TonetraceKernel k;
        k.prepare(tk::kRate, tk::kBlock);
        setTranscriber(std::make_shared<tk::FakeTranscriber>(std::vector<std::vector<TranscribedNote>>{{{0.5, 1.0, 60, false, 0}}}));
        CHECK(k.loadSong("/tmp/Restore.wav") && tk::waitIdle(k));
        k.setInstrumentMix(0, {false, false, 0.3f});
        k.setExportBpm(140.0);
        saved = k.saveState();
        k.deactivate();
    }
    TonetraceKernel k;
    k.prepare(tk::kRate, tk::kBlock);
    CHECK(k.loadState(saved.data(), saved.size()));
    CHECK(tk::waitIdle(k));
    const DocumentPtr doc = k.document();
    CHECK(doc && doc->sourceName == "Restore.wav" && doc->notes.size() == 1 && doc->audio && doc->peaks);
    CHECK(k.exportBpm() == 140.0 && k.mixer().get(0).gain == 0.3f);
    setAudioDecoder(nullptr);  // the file is gone: the notes stay, a notice says why
    CHECK(k.loadState(saved.data(), saved.size()) && tk::waitIdle(k));
    CHECK(k.document() && k.document()->notes.size() == 1 && !k.document()->audio);
    k.deactivate();
    setTranscriber(nullptr);
}

TEST(a_restored_recording_keeps_its_audio_in_the_state) {
    TonetraceKernel k;
    k.prepare(tk::kRate, tk::kBlock);
    setTranscriber(std::make_shared<tk::FakeTranscriber>(std::vector<std::vector<TranscribedNote>>{{{0.1, 0.5, 64, false, 0}}}));
    k.setParam(kParamRecord, 1.0f);
    tk::run(k, tk::sine(440.0, 1.0));
    k.setParam(kParamRecord, 0.0f);
    tk::run(k, tk::silence(2));
    CHECK(tk::waitFor([&] { return k.document() && k.document()->transcription.phase == TranscriptionPhase::Done; }));
    const std::vector<uint8_t> saved = k.saveState();
    CHECK(saved.size() < 100000);  // 1 s mono 22.05 kHz 16 bit
    TonetraceKernel other;
    other.prepare(tk::kRate, tk::kBlock);
    CHECK(other.loadState(saved.data(), saved.size()));
    const DocumentPtr doc = other.document();
    CHECK(doc && doc->isRecording && doc->audio && doc->audio->channels == 1 && std::fabs(doc->lengthSec - 1.0) < 0.02 && doc->notes.size() == 1);
    k.deactivate();
    other.deactivate();
    setTranscriber(nullptr);
}

TEST(old_and_damaged_states_are_refused_with_a_reason) {
    const std::vector<uint8_t> valid = encodeState(defaultParams(), sampleDocument().get(), MixerState{}, 0.0);
    DecodedState out;
    std::string reason;
    std::vector<uint8_t> old = valid;
    old[4] = 5;  // Tonetrace 4.7
    CHECK(!decodeState(old.data(), old.size(), &out, &reason) && reason.find("Tonetrace 4") != std::string::npos);
    std::vector<uint8_t> newer = valid;
    newer[4] = 9;
    CHECK(!decodeState(newer.data(), newer.size(), &out, &reason) && reason.find("newer") != std::string::npos);
    std::vector<uint8_t> flipped = valid;
    flipped[40] ^= 1;
    CHECK(!decodeState(flipped.data(), flipped.size(), &out, &reason) && reason.find("checksum") != std::string::npos);
    CHECK(!decodeState(valid.data(), valid.size() - 3, &out, &reason));
    CHECK(!decodeState(nullptr, 0, &out, &reason) && reason.find("empty") != std::string::npos);
    TonetraceKernel k;
    CHECK(!k.loadState(old.data(), old.size()));
    Notice n;
    CHECK(k.diagnostics().popNotice(&n) && n.level == NoticeLevel::Error);
}

TEST(random_damage_never_crashes_the_decoder) {
    const std::vector<uint8_t> valid = encodeState(defaultParams(), sampleDocument().get(), MixerState{}.with(33, {true, false, 0.4f}), 120.0);
    std::mt19937 rng(0xC0FFEE);
    int accepted = 0;
    for (int i = 0; i < 3000; ++i) {
        std::vector<uint8_t> blob = valid;
        const int flips = 1 + int(rng() % 8);
        for (int f = 0; f < flips; ++f) blob[16 + rng() % (blob.size() - 16)] = uint8_t(rng());
        if (i % 3 == 0) blob.resize(16 + rng() % (blob.size() - 16));
        const uint32_t length = uint32_t(blob.size() - 16), crc = crc32(blob.data() + 16, length);  // a valid header over damaged data
        std::memcpy(&blob[8], &length, 4);
        std::memcpy(&blob[12], &crc, 4);
        DecodedState out;
        std::string reason;
        if (decodeState(blob.data(), blob.size(), &out, &reason)) {
            ++accepted;
            if (out.document)
                for (const NoteEvent& n : out.document->notes) CHECK(n.endSec >= n.startSec && n.pitch >= 0 && n.pitch <= 127);
        } else {
            CHECK(!reason.empty());
        }
    }
    CHECK(accepted < 3000);
}

TEST(editor_settings_round_trip_and_reject_foreign_data) {
    UiSettings s;
    s.pixelsPerSecond = 80.0;
    s.rowHeight = 18.0;
    s.scrollSec = 12.5;
    s.scrollY = 300.0;
    s.width = 1500;
    s.height = 900;
    s.followPlayhead = false;
    const std::vector<uint8_t> bytes = encodeUiSettings(s);
    UiSettings back;
    CHECK(decodeUiSettings(bytes.data(), bytes.size(), &back) && back == s);
    UiSettings wild;
    wild.pixelsPerSecond = 1e9;
    wild.rowHeight = NAN;
    wild.width = 5;
    const UiSettings clean = sanitized(wild);
    CHECK(clean.pixelsPerSecond == kMaxPixelsPerSecond && clean.rowHeight == kDefaultRowHeight && clean.width == kMinWindowWidth);
    std::vector<uint8_t> foreign = bytes;
    foreign[3] = 0x31;  // "TTU1": a Tonetrace 4 editor state
    CHECK(!decodeUiSettings(foreign.data(), foreign.size(), &back));
}
