// The kernel as the user meets it: load a song, hear it while the model works, watch the notes arrive chunk by chunk,
// mix the instruments, record instead of loading, cancel, wait for the model download. Plus real-time safety.
#include <cmath>

#include "AllocCounter.h"
#include "TestKernel.h"
#include "TestUtil.h"
#include "core/Instruments.h"
#include "midi/ExportPlanner.h"
#include "preview/NoteSynth.h"

using namespace ttr;

namespace {

// Three chunks: a bass note, a drum hit, a piano note.
std::vector<std::vector<TranscribedNote>> threeChunks() {
    return {{{0.10, 1.90, 45, false, 33}}, {{1.00, 1.01, 36, true, 128}}, {{2.00, 2.80, 72, false, 0}}};
}

struct Fixture {
    explicit Fixture(bool gated = false, double seconds = 3.0) : model(std::make_shared<tk::FakeTranscriber>(threeChunks(), gated)) {
        tk::installFakeDecoder(seconds);
        setTranscriber(model);
        k.prepare(tk::kRate, tk::kBlock);
    }
    ~Fixture() {
        model->releaseAll();
        k.deactivate();
        k.clearDocument();
        setTranscriber(nullptr);
        setAudioDecoder(nullptr);
    }
    std::shared_ptr<tk::FakeTranscriber> model;
    TonetraceKernel k;
};

DocumentPtr waitForDoc(TonetraceKernel& k, const std::function<bool(const Document&)>& cond) {
    DocumentPtr doc;
    tk::waitFor([&] { doc = k.document(); return doc && cond(*doc); });
    return doc;
}

bool hasNotice(TonetraceKernel& k, const std::string& part) {
    Notice n;
    while (k.diagnostics().popNotice(&n))
        if (n.text.find(part) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(the_synth_plays_tones_and_drums_and_reports_levels_per_instrument) {
    NoteSynth synth;
    synth.prepare(tk::kRate);
    std::vector<float> out(4096, 0.0f);
    synth.noteOn(57, 33, 1.0f, 0.05, 100);
    synth.noteOn(36, kDrumProgram, 1.0f, 0.01, 0);
    synth.render(out.data(), 4096);
    CHECK(out[50] != 0.0f);  // the kick starts at once
    CHECK(synth.level(33 + 1) > 0.01f && synth.level(kDrumProgram + 1) > 0.01f && synth.level(1) == 0.0f);
    synth.reset();
    std::vector<float> delayed(512, 0.0f);
    synth.noteOn(69, 0, 1.0f, 0.5, 300);  // starts 300 samples into the block
    synth.render(delayed.data(), 512);
    CHECK(delayed[299] == 0.0f && std::fabs(delayed[400]) > 0.0f);
    std::vector<float> rest(44100, 0.0f);
    synth.render(rest.data(), 44100);
    CHECK(!synth.isActive());  // released after its length
}

TEST(a_loaded_song_plays_while_the_notes_arrive_chunk_by_chunk) {
    Fixture f(true);
    CHECK(f.k.loadSong("/tmp/My Song.wav"));
    DocumentPtr doc = waitForDoc(f.k, [](const Document& d) { return d.audio != nullptr; });
    CHECK(doc && doc->transcription.phase == TranscriptionPhase::Running && doc->notes.empty() && std::fabs(doc->lengthSec - 3.0) < 0.01);
    CHECK(doc && doc->sourceName == "My Song.wav" && doc->peaks && doc->peaks->count() > 250);
    // playable before any note exists
    f.k.setParam(kParamMix, 0.0f);  // original only
    f.k.play();
    const std::vector<float> heard = tk::run(f.k, tk::silence(40));
    CHECK(f.k.isPlaying() && tk::rms(heard) > 0.1 && f.k.positionSec() > 0.4);
    // chunk 1: the bass note appears, chunk 2: the drum hit
    f.model->release();
    doc = waitForDoc(f.k, [](const Document& d) { return d.notes.size() == 1; });
    CHECK(doc && doc->notes[0].program == 33 && doc->transcription.progress > 0.3f && doc->transcription.finalizedSec == 5.0);
    f.model->release();
    doc = waitForDoc(f.k, [](const Document& d) { return d.notes.size() == 2; });
    CHECK(doc && doc->transcription.phase == TranscriptionPhase::Running);
    f.model->release();
    doc = waitForDoc(f.k, [](const Document& d) { return d.transcription.phase == TranscriptionPhase::Done; });
    CHECK(doc && doc->notes.size() == 3 && instrumentsIn(doc->notes).size() == 3 && doc->transcription.progress == 1.0f);
    CHECK(std::fabs(double(f.model->samples) - 3.0 * kMuScriptorRate) < 4.0);  // the model got the whole song at 16 kHz
}

TEST(the_notes_play_through_the_synth_with_the_mixer_applied) {
    Fixture f;
    CHECK(f.k.loadSong("/tmp/song.wav") && tk::waitIdle(f.k));
    const DocumentPtr doc = f.k.document();
    CHECK(doc && doc->notes.size() == 3);
    f.k.setParam(kParamMix, 1.0f);  // MIDI only
    f.k.play();
    const std::vector<float> withBass = tk::run(f.k, tk::silence(60));  // 0..0.7 s: the bass note
    CHECK(tk::rms(withBass) > 0.01);
    CHECK(f.k.instrumentLevel(33) > 0.0f);
    f.k.setInstrumentMix(33, {true, false, 0.8f});  // mute the bass
    f.k.seek(0.2);
    tk::run(f.k, tk::silence(2));  // the old voice fades out (3 ms)
    CHECK(tk::rms(tk::run(f.k, tk::silence(40))) < 1e-5);
    f.k.setInstrumentMix(33, {false, false, 0.8f});
    f.k.setParam(kParamMix, 0.0f);  // original only: the sine of the fake song
    f.k.seek(0.2);
    CHECK(tk::rms(tk::run(f.k, tk::silence(20))) > 0.1);
}

TEST(playback_stops_at_the_end_and_play_starts_again_from_the_beginning) {
    Fixture f(false, 1.0);
    CHECK(f.k.loadSong("/tmp/short.wav") && tk::waitIdle(f.k));
    f.k.play();
    tk::run(f.k, tk::silence(100));  // 1.16 s
    CHECK(!f.k.isPlaying() && std::fabs(f.k.positionSec() - 1.0) < 1e-6);
    f.k.play();
    tk::run(f.k, tk::silence(2));
    CHECK(f.k.isPlaying() && f.k.positionSec() < 0.1);
    f.k.seek(99.0);
    CHECK(f.k.positionSec() == 1.0);  // clamped to the song
    f.k.pause();
    CHECK(!f.k.isPlaying());
}

TEST(mute_input_silences_the_incoming_signal_and_bypass_leaves_it_alone) {
    Fixture f;
    const std::vector<float> input = tk::sine(440.0, 0.3);
    CHECK(tk::rms(tk::run(f.k, input)) > 0.2);  // nothing loaded: the plug-in passes the input
    f.k.setParam(kParamMuteInput, 1.0f);
    CHECK(tk::rms(tk::run(f.k, input)) < 1e-6);
    f.k.setParam(kParamBypass, 1.0f);
    CHECK(tk::rms(tk::run(f.k, input)) > 0.2);
}

TEST(a_recording_becomes_the_document_and_is_transcribed) {
    Fixture f;
    f.k.setParam(kParamRecord, 1.0f);
    tk::run(f.k, tk::sine(330.0, 1.0));
    CHECK(f.k.isRecording() && f.k.recordedSeconds() > 0.9);
    CHECK(tk::waitFor([&] { return f.k.recordingView().count > 50; }));  // the live waveform
    f.k.setParam(kParamRecord, 0.0f);
    tk::run(f.k, tk::silence(2));
    const DocumentPtr doc = waitForDoc(f.k, [](const Document& d) { return d.transcription.phase == TranscriptionPhase::Done; });
    CHECK(doc && doc->isRecording && doc->sourceName == "Recording" && doc->audio && std::fabs(doc->lengthSec - 0.998) < 0.02);
    CHECK(doc && doc->notes.size() == 3);
    f.k.setParam(kParamRecord, 1.0f);
    tk::run(f.k, tk::silence(4));  // 46 ms: too short
    f.k.setParam(kParamRecord, 0.0f);
    tk::run(f.k, tk::silence(2));
    CHECK(tk::waitFor([&] { return hasNotice(f.k, "too short"); }));
    CHECK(f.k.document() == doc);  // the last recording stays
}

TEST(cancel_keeps_the_notes_found_so_far_and_a_new_song_replaces_a_running_one) {
    Fixture f(true);
    CHECK(f.k.loadSong("/tmp/first.wav"));
    f.model->release();
    CHECK(waitForDoc(f.k, [](const Document& d) { return d.notes.size() == 1; }) != nullptr);
    f.k.cancelTranscription();
    DocumentPtr doc = waitForDoc(f.k, [](const Document& d) { return d.transcription.phase == TranscriptionPhase::Cancelled; });
    CHECK(doc && doc->notes.size() == 1 && doc->audio);
    CHECK(tk::waitIdle(f.k));
    f.model->releaseAll();
    CHECK(f.k.loadSong("/tmp/second.wav") && f.k.loadSong("/tmp/third.wav"));  // the second is replaced at once
    doc = waitForDoc(f.k, [](const Document& d) { return d.transcription.phase == TranscriptionPhase::Done; });
    CHECK(doc && doc->sourceName == "third.wav" && doc->notes.size() == 3);
    CHECK(!f.k.loadSong("/tmp/notes.mid"));
    f.k.clearDocument();
    CHECK(!f.k.document() && !f.k.isWorking());
}

TEST(a_broken_file_or_a_missing_model_is_reported_on_the_document) {
    Fixture f;
    CHECK(f.k.loadSong("/tmp/broken.wav") && tk::waitIdle(f.k));
    DocumentPtr doc = f.k.document();
    CHECK(doc && doc->transcription.phase == TranscriptionPhase::Failed && doc->transcription.message == "the file is damaged" && !doc->audio);
    setTranscriberLoader([](std::string* reason) { *reason = "Tonetrace needs an Apple Silicon Mac"; return std::shared_ptr<NoteTranscriber>(); });
    CHECK(f.k.loadSong("/tmp/song.wav") && tk::waitIdle(f.k));
    doc = f.k.document();
    CHECK(doc && doc->transcription.phase == TranscriptionPhase::Failed && doc->transcription.message.find("Apple Silicon") != std::string::npos);
    CHECK(doc && doc->audio);  // the song still plays
    CHECK(hasNotice(f.k, "Transcription unavailable"));
    setTranscriberLoader(nullptr);
}

TEST(the_transcription_waits_for_the_model_download_and_then_starts) {
    Fixture f;
    auto model = f.model;
    std::atomic<int> asks{0};
    setTranscriberLoader([&asks, model](std::string* reason) -> std::shared_ptr<NoteTranscriber> {
        if (++asks < 4) { *reason = "model downloading (" + std::to_string(asks * 25) + " % of 620 MB)"; return nullptr; }
        return model;
    });
    CHECK(f.k.loadSong("/tmp/song.wav"));
    DocumentPtr doc = waitForDoc(f.k, [](const Document& d) { return d.transcription.message.find("downloading") != std::string::npos; });
    CHECK(doc && doc->transcription.phase == TranscriptionPhase::Running && doc->audio);
    doc = waitForDoc(f.k, [](const Document& d) { return d.transcription.phase == TranscriptionPhase::Done; });
    CHECK(doc && doc->notes.size() == 3 && doc->transcription.message.empty() && asks >= 4);
    setTranscriberLoader(nullptr);
}

TEST(the_kernel_exports_the_audible_instruments_at_the_export_tempo) {
    Fixture f;
    CHECK(f.k.loadSong("/tmp/My Song.wav") && tk::waitIdle(f.k));
    f.k.setExportBpm(60.0);
    ParsedMidi midi = parseMidiFile(f.k.buildMidi());
    CHECK(midi.valid && midi.tracks == 4 && std::fabs(midi.bpm - 60.0) < 0.01 && midi.trackNames[0] == "My Song Tempo");
    f.k.setInstrumentMix(kDrumProgram, {true, false, 0.8f});
    midi = parseMidiFile(f.k.buildMidi());
    CHECK(midi.tracks == 3 && midi.notes.size() == 2);
    CHECK(parseMidiFile(f.k.buildMidi(true)).notes.size() == 3);
    CHECK(f.k.midiFileName() == "My Song Tonetrace 60bpm.mid");
    f.k.setExportBpm(NAN);
    CHECK(f.k.exportBpm() == 60.0);
}

TEST(the_audio_thread_never_allocates_while_playing_recording_and_streaming) {
    Fixture f(true);
    CHECK(f.k.loadSong("/tmp/song.wav"));
    CHECK(waitForDoc(f.k, [](const Document& d) { return d.audio != nullptr; }) != nullptr);
    f.k.setParam(kParamMix, 0.5f);
    f.k.play();
    std::vector<float> l(tk::kBlock, 0.1f), r(tk::kBlock, 0.1f);
    float* ch[2] = {l.data(), r.data()};
    f.k.process(ch, 2, tk::kBlock);  // warm-up
    tt::AllocCounter::start();
    for (int i = 0; i < 200; ++i) {
        if (i == 20 || i == 60 || i == 100) f.model->release();  // notes arrive while the audio thread plays
        if (i == 150) f.k.seek(0.5);
        f.k.process(ch, 2, tk::kBlock);
    }
    const long allocations = tt::AllocCounter::stop();
    CHECK_MSG(allocations == 0, "%ld allocations on the audio thread", allocations);
    f.k.pause();
    f.k.setParam(kParamRecord, 1.0f);
    tt::AllocCounter::start();
    for (int i = 0; i < 50; ++i) f.k.process(ch, 2, tk::kBlock);
    CHECK(tt::AllocCounter::stop() == 0);
    f.k.setParam(kParamRecord, 0.0f);
}
