// 4.7: whole-song import with one MIDI track per instrument (NeuralNote-style): instruments, multi-track MIDI, state v5
// and the kernel's import task with a fake decoder and a fake transcriber.
#include <cmath>
#include <memory>

#include "TestKernel.h"
#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/ml/MuScriptorEngine.h"
#include "core/Instruments.h"
#include "core/NoteEdit.h"
#include "core/SongImport.h"
#include "midi/ExportPlanner.h"
#include "state/StateCodec.h"

using namespace ttr;

namespace {

constexpr double kRate = 44100.0;

class ProgramTranscriber : public NoteTranscriber {
public:
    explicit ProgramTranscriber(std::vector<TranscribedNote> notes) : notes_(std::move(notes)) {}
    bool transcribe(const std::vector<float>& audio, std::vector<TranscribedNote>* out, TaskControl*, std::string*) override {
        samples = audio.size();
        *out = notes_;
        return true;
    }
    size_t samples = 0;

private:
    std::vector<TranscribedNote> notes_;
};

NoteEvent note(int pitch, double start, double end, int program) {
    NoteEvent n;
    n.pitch = pitch;
    n.startSec = start;
    n.endSec = end;
    n.program = program;
    return n;
}

// A decoder that "reads" a tone mix of `seconds` for any path, and fails for paths containing "broken".
void installFakeDecoder(double seconds) {
    setAudioDecoder([seconds](const std::string& path, double rate, double maxSeconds, DecodedAudio* out, std::string* reason) {
        if (path.find("broken") != std::string::npos) { *reason = "the file is damaged"; return false; }
        const double used = std::min(seconds, maxSeconds);
        out->mono = tsig::synth({{45, 0.2, used - 0.2, 0.4f}, {69, 0.5, 1.5, 0.3f}}, rate, used, true);
        out->sampleRate = rate;
        out->fileSeconds = seconds;
        out->truncated = seconds > maxSeconds;
        return true;
    });
}

bool waitForImport(TonetraceKernel& k) { return tk::waitFor([&] { return !k.quickGrabRunning(); }, 60000); }

std::vector<std::string> drainNotices(TonetraceKernel& k) {
    std::vector<std::string> out;
    Notice n;
    while (k.diagnostics().popNotice(&n)) out.push_back(n.text);
    return out;
}

bool anyContains(const std::vector<std::string>& texts, const std::string& part) {
    for (const std::string& t : texts) if (t.find(part) != std::string::npos) return true;
    return false;
}

}  // namespace

TEST(instruments_have_names_families_and_a_summary) {
    CHECK(instrumentName(33) == "Bass" && instrumentName(kDrumProgram) == "Drums" && instrumentName(88) == "Synth Pad");
    CHECK(instrumentName(kNoProgram) == "Notes" && instrumentName(97) == "Program 97");
    CHECK(instrumentFamily(0) == InstrumentFamily::Keys && instrumentFamily(33) == InstrumentFamily::Bass);
    CHECK(instrumentFamily(kDrumProgram) == InstrumentFamily::Drums && instrumentFamily(kNoProgram) == InstrumentFamily::None);
    CHECK(isValidProgram(kNoProgram) && isValidProgram(kDrumProgram) && !isValidProgram(129) && !isValidProgram(-2));
    const std::vector<NoteEvent> notes = {note(36, 0, 0.1, kDrumProgram), note(40, 0, 1, 33), note(41, 1, 2, 33), note(60, 0, 1, kNoProgram)};
    const std::vector<InstrumentSummary> all = instrumentsIn(notes);
    CHECK(all.size() == 3 && all[0].program == kNoProgram && all[1].program == 33 && all[1].noteCount == 2 && all[2].program == kDrumProgram);
    CHECK(hasInstruments(notes) && !hasInstruments({note(60, 0, 1, kNoProgram)}));
    const std::vector<NoteEvent> bassOnly = withInstrumentSelected(notes, 33);
    CHECK(selectedCount(bassOnly) == 2 && !notes[1].selected);  // the input stays unselected
}

TEST(two_instruments_on_the_same_note_are_not_merged) {
    const std::vector<NoteEvent> merged = normalizedNotes({note(60, 0.0, 1.0, 0), note(60, 0.5, 1.5, 48), note(60, 0.8, 2.0, 0)});
    CHECK(merged.size() == 2);
    for (const NoteEvent& n : merged) {
        if (n.program == 0) CHECK(n.startSec == 0.0 && n.endSec == 2.0);  // same instrument: merged
        else CHECK(n.program == 48 && n.startSec == 0.5);
    }
}

TEST(multi_track_midi_has_one_track_per_instrument_and_drums_on_channel_10) {
    std::vector<MidiNote> notes;
    for (int program : {33, kDrumProgram, 0, 88, kNoProgram}) {
        MidiNote m{48, 1.0, 0.5, 100};
        m.program = program;
        notes.push_back(m);
    }
    const std::vector<MidiTrack> tracks = planTracks(notes);
    CHECK(tracks.size() == 5);
    CHECK(tracks[0].name == "Notes" && tracks[1].name == "Piano" && tracks[2].name == "Bass" && tracks[4].name == "Drums");
    CHECK(tracks[4].channel == kDrumChannel && tracks[4].program == 0);
    for (size_t i = 0; i + 1 < tracks.size(); ++i) CHECK(tracks[i].channel != kDrumChannel);
    const ParsedMidi parsed = parseMidiFile(writeMidiTracks(tracks, 100.0, "Song", 8.0));
    CHECK(parsed.valid && parsed.format == 1 && parsed.tracks == 6 && parsed.notes.size() == 5);
    CHECK(parsed.trackNames.size() == 6 && parsed.trackNames[0] == "Song Tempo" && parsed.trackNames[3] == "Bass");
    CHECK(std::fabs(parsed.bpm - 100.0) < 0.01 && std::fabs(parsed.endBeat - 8.0) < 1e-9);
    int drums = 0, bass = 0;
    for (const MidiNote& n : parsed.notes) {
        if (n.channel == kDrumChannel) ++drums;
        if (n.program == 33) ++bass;
    }
    CHECK(drums == 1 && bass == 1);
}

TEST(back_to_back_notes_of_one_key_all_survive_tick_rounding) {
    std::vector<MidiNote> notes;
    for (int i = 0; i < 50; ++i) notes.push_back({60, i * 0.3337, 0.3341, 100});  // each ends a hair after the next starts
    const ParsedMidi parsed = parseMidiFile(writeMidiFile(notes, 120.0, "Repeats"));
    CHECK(parsed.valid && parsed.notes.size() == notes.size());
    for (const MidiNote& n : parsed.notes) CHECK(n.lengthBeat > 0.3);
}

TEST(sixteen_melodic_instruments_share_channels_but_never_the_drum_channel) {
    std::vector<MidiNote> notes;
    for (int program = 0; program < 20; ++program) {
        MidiNote m;
        m.program = program;
        notes.push_back(m);
    }
    const std::vector<MidiTrack> tracks = planTracks(notes);
    CHECK(tracks.size() == 20);
    for (const MidiTrack& t : tracks) CHECK(t.channel != kDrumChannel && t.channel >= 0 && t.channel < 16);
    CHECK(tracks[15].channel == tracks[9].channel);  // the 16th reuses the first of the last six
}

TEST(song_export_starts_at_the_song_start_and_is_named_after_it) {
    Recording rec;
    rec.bpm = 120.0;
    rec.sourceName = "My Song.mp3";
    const SnapshotPtr snap = makeSnapshot({note(40, 4.0, 5.0, 33), note(36, 6.0, 6.1, kDrumProgram)}, rec, 1);
    const ParsedMidi parsed = parseMidiFile(buildMidiForNotes(*snap, 0, snap->key));
    CHECK(parsed.valid && parsed.tracks == 3 && !parsed.trackNames.empty() && parsed.trackNames[0] == "My Song Tempo");
    CHECK(!parsed.notes.empty() && std::fabs(parsed.notes[0].startBeat - 8.0) < 1e-9);  // 4 s at 120 BPM, not moved to 0
    CHECK(midiFileNameFor(*snap) == "My Song Tonetrace 120bpm.mid");
    const SnapshotPtr recorded = makeSnapshot({note(60, 4.0, 5.0, kNoProgram)}, Recording{}, 1);
    const ParsedMidi plain = parseMidiFile(buildMidiForNotes(*recorded, 0, recorded->key));
    CHECK(plain.tracks == 2 && plain.notes.size() == 1 && plain.notes[0].startBeat == 0.0);  // unchanged single-track export
}

TEST(state_v5_keeps_instruments_and_the_song_name_and_v4_still_loads) {
    Recording rec;
    rec.bpm = 98.0;
    rec.sourceName = "Track.wav";
    const SnapshotPtr snap = makeSnapshot({note(40, 0.0, 1.0, 33), note(38, 0.5, 0.6, kDrumProgram), note(64, 1.0, 2.0, kNoProgram)}, rec, 3);
    const std::vector<uint8_t> v5 = encodeState(defaultParams(), snap.get());
    DecodedState out;
    std::string reason;
    CHECK_MSG(decodeState(v5.data(), v5.size(), &out, &reason), "%s", reason.c_str());
    CHECK(out.formatVersion == kStateFormatVersion && out.snapshot && out.snapshot->sourceName == "Track.wav");
    CHECK(out.snapshot && instrumentsIn(out.snapshot->notes).size() == 3);
    const std::vector<uint8_t> v4 = encodeStateAs(defaultParams(), snap.get(), false, 4);
    DecodedState old;
    CHECK(decodeState(v4.data(), v4.size(), &old) && old.formatVersion == 4 && old.snapshot && old.snapshot->notes.size() == 3);
    CHECK(old.snapshot && !hasInstruments(old.snapshot->notes) && old.snapshot->sourceName.empty());
}

TEST(a_long_song_state_leaves_out_waveform_and_audio_but_loads) {
    Recording rec;
    rec.sourceName = "Long.wav";
    auto peaks = std::make_shared<WavePeaks>();
    peaks->minMax.assign(2 * 120000, 0.1f);  // 10 minutes at 200 per second
    rec.peaks = peaks;
    auto clip = std::make_shared<AudioClip>();
    clip->samples.assign(13u * 1000u * 1000u, 0);  // 10 minutes at 22 kHz
    rec.audio = clip;
    const SnapshotPtr snap = makeSnapshot({note(40, 0.0, 1.0, 33)}, rec, 1);
    const std::vector<uint8_t> bytes = encodeState(defaultParams(), snap.get());
    DecodedState out;
    std::string reason;
    CHECK_MSG(decodeState(bytes.data(), bytes.size(), &out, &reason), "%s", reason.c_str());
    CHECK(out.snapshot && !out.snapshot->peaks && !out.snapshot->audio && out.snapshot->notes.size() == 1);
    CHECK(bytes.size() < 1024 * 1024);
}

TEST(muscriptor_notes_carry_their_instrument) {
    const std::vector<NoteEvent> notes = muscriptorNotes({{0.0, 1.0, 40, false, 33}, {0.5, 0.51, 36, true, 128}, {1.0, 2.0, 60, false, 300}}, 120.0);
    CHECK(notes.size() == 3);
    for (const NoteEvent& n : notes) {
        if (n.pitch == 40) CHECK(n.program == 33);
        if (n.pitch == 36) CHECK(n.program == kDrumProgram);
        if (n.pitch == 60) CHECK(n.program == kNoProgram);  // out of range: no instrument rather than a wrong one
    }
}

TEST(song_files_are_recognised_and_bad_decodes_give_reasons) {
    CHECK(isSongFile("/a/b/Song.MP3") && isSongFile("x.flac") && isSongFile("y.aiff") && !isSongFile("notes.mid") && !isSongFile("wav"));
    CHECK(fileNameOf("/Users/x/My Song.wav") == "My Song.wav");
    DecodedAudio audio;
    std::string why;
    CHECK(!decodeAudioFile("song.txt", &audio, &why) && why.find("unsupported") != std::string::npos);
    setAudioDecoder([](const std::string&, double rate, double, DecodedAudio* out, std::string*) {
        out->mono = {0.1f, NAN};
        out->sampleRate = rate;
        return true;
    });
    CHECK(!decodeAudioFile("song.wav", &audio, &why) && why.find("no audio") != std::string::npos);
    setAudioDecoder(nullptr);
    CHECK(!decodeAudioFile("song.wav", &audio, &why) && why.find("decoder") != std::string::npos);
}

TEST(importing_a_song_transcribes_every_instrument_as_one_undo_step) {
    installFakeDecoder(3.0);
    auto fake = std::make_shared<ProgramTranscriber>(std::vector<TranscribedNote>{
        {0.2, 2.8, 45, false, 33}, {0.5, 1.5, 69, false, 0}, {1.0, 1.01, 36, true, 128}, {2.0, 2.01, 36, true, 128}});
    setTranscriber(fake);
    TonetraceKernel k;
    k.prepare(kRate, tk::kBlock);
    k.editNotes({note(72, 0.0, 0.5, kNoProgram)});  // an earlier document
    CHECK(!k.startSongImport("/tmp/notes.mid"));    // not audio
    CHECK(k.startSongImport("/tmp/My Song.wav"));
    CHECK(!k.startQuickGrab() && !k.startSongImport("/tmp/other.wav"));  // one task at a time
    CHECK(waitForImport(k));
    const SnapshotPtr snap = k.snapshot();
    CHECK(snap && snap->hasRecording() && snap->sourceName == "My Song.wav" && std::fabs(snap->lengthSec - 3.0) < 0.05);
    CHECK(snap && snap->notes.size() == 4 && instrumentsIn(snap->notes).size() == 3);
    CHECK(std::fabs(double(fake->samples) - 3.0 * kMuScriptorRate) < 4.0);  // the whole song reached the model at 16 kHz
    const std::vector<std::string> notices = drainNotices(k);
    CHECK(anyContains(notices, "Bass") && anyContains(notices, "Drums") && anyContains(notices, "one track per instrument"));
    CHECK(k.undo() && k.snapshot() && k.snapshot()->notes.empty() && k.snapshot()->sourceName == "My Song.wav");  // the notes
    CHECK(k.undo() && k.snapshot() && k.snapshot()->notes.size() == 1 && k.snapshot()->notes[0].pitch == 72);   // the old document
    k.deactivate();
    setTranscriber(nullptr);
    setAudioDecoder(nullptr);
}

TEST(song_import_without_the_model_uses_classic_and_says_why) {
    installFakeDecoder(3.0);
    setTranscriberLoader([](std::string* reason) { *reason = "MuScriptor model not downloaded yet"; return std::shared_ptr<NoteTranscriber>(); });
    TonetraceKernel k;
    k.prepare(kRate, tk::kBlock);
    CHECK(k.startSongImport("/tmp/song.wav") && waitForImport(k));
    const SnapshotPtr snap = k.snapshot();
    CHECK(snap && snap->hasRecording() && !snap->notes.empty() && !hasInstruments(snap->notes));
    CHECK(anyContains(drainNotices(k), "not downloaded yet"));
    k.deactivate();
    setTranscriberLoader(nullptr);
    setAudioDecoder(nullptr);
}

TEST(a_broken_song_is_reported_and_cancel_returns_at_once) {
    installFakeDecoder(3.0);
    TonetraceKernel k;
    k.prepare(kRate, tk::kBlock);
    CHECK(k.startSongImport("/tmp/broken.wav") && waitForImport(k));
    CHECK(!k.snapshot() && anyContains(drainNotices(k), "the file is damaged"));
    installFakeDecoder(20.0);
    CHECK(k.startSongImport("/tmp/long.wav"));
    k.requestCancelQuickGrab();  // returns at once, the task ends by itself without publishing the song
    CHECK(waitForImport(k));
    CHECK(!k.snapshot());
    k.deactivate();
    setAudioDecoder(nullptr);
}
