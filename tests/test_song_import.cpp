// Instruments, model-note conversion, one MIDI track per instrument and the song file checks.
#include <chrono>
#include <cmath>

#include "TestUtil.h"
#include "analysis/ml/MuScriptorEngine.h"
#include "core/Instruments.h"
#include "core/Mixer.h"
#include "core/SongImport.h"
#include "midi/ExportPlanner.h"

using namespace ttr;

namespace {

NoteEvent note(int pitch, double start, double end, int program) {
    NoteEvent n;
    n.pitch = pitch;
    n.startSec = start;
    n.endSec = end;
    n.program = program;
    return n;
}

}  // namespace

TEST(instruments_have_names_chips_families_and_ranges) {
    CHECK(instrumentName(33) == "Bass" && instrumentName(kDrumProgram) == "Drums" && instrumentName(88) == "Synth Pad");
    CHECK(instrumentName(97) == "Program 97");
    CHECK(instrumentAbbreviation(33) == "BAS" && instrumentAbbreviation(kDrumProgram) == "DRM" && instrumentAbbreviation(26) == "GTR");
    CHECK(instrumentFamily(0) == InstrumentFamily::Keys && instrumentFamily(kDrumProgram) == InstrumentFamily::Drums);
    const std::vector<NoteEvent> notes = {note(36, 0, 0.1, kDrumProgram), note(40, 0, 1, 33), note(55, 1, 2, 33)};
    const std::vector<InstrumentSummary> all = instrumentsIn(notes);
    CHECK(all.size() == 2 && all[0].program == 33 && all[0].noteCount == 2 && all[1].program == kDrumProgram);
    int lo = 0, hi = 0;
    CHECK(pitchRangeOf(notes, 33, &lo, &hi) && lo == 40 && hi == 55 && !pitchRangeOf(notes, 0, &lo, &hi));
    CHECK(noteName(40) == "E2" && noteName(61) == "C#4" && noteName(21) == "A0");
}

TEST(model_notes_become_instrument_notes_with_short_drum_hits) {
    const std::vector<NoteEvent> notes = muscriptorNotes(
        {{1.00, 1.50, 45, false, 33}, {0.50, 0.51, 36, true, 128}, {0.55, 0.56, 42, true, 128}, {0.60, 0.61, 42, true, 128}, {0.2, 0.4, 10, false, 0},
         {2.0, 3.0, 60, false, 300}},
        120.0);
    CHECK(notes.size() == 5);  // pitch 10 is below the piano roll
    CHECK(notes[0].pitch == 36 && notes[0].program == kDrumProgram && std::abs(notes[0].endSec - 0.625) < 1e-9);  // 1/16 at 120 BPM
    CHECK(notes[1].pitch == 42 && std::abs(notes[1].endSec - 0.60) < 1e-9);  // capped at the next hit of the same drum
    CHECK(notes[3].pitch == 45 && notes[3].program == 33 && notes[3].endSec == 1.50);
    CHECK(notes[4].program == kNoProgram);  // out of range: no instrument rather than a wrong one
    CHECK(std::abs(muscriptorNotes({{0.0, 0.01, 38, true, 128}}, 0.0)[0].endSec - 0.125) < 1e-9);  // no tempo: 120 BPM
}

TEST(drum_lengths_stay_fast_for_a_long_song) {
    std::vector<TranscribedNote> raw;
    for (int i = 0; i < 20000; ++i) raw.push_back({i * 0.03, i * 0.03 + 0.01, 36 + (i % 10), true, 128});
    const auto started = std::chrono::steady_clock::now();
    const std::vector<NoteEvent> notes = muscriptorNotes(raw, 128.0);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    CHECK(notes.size() == raw.size());
    CHECK_MSG(ms < 200.0, "%.0f ms for 20000 hits", ms);  // runs after every chunk while the notes stream in
}

TEST(midi_has_one_track_per_instrument_and_drums_on_channel_10) {
    const std::vector<NoteEvent> notes = {note(40, 0.0, 1.0, 33), note(36, 0.5, 0.6, kDrumProgram), note(60, 1.0, 2.0, 0), note(64, 2.0, 2.5, 88)};
    const ParsedMidi parsed = parseMidiFile(buildMidi(notes, 120.0, "My Song"));
    CHECK(parsed.valid && parsed.format == 1 && parsed.tracks == 5 && parsed.notes.size() == 4);
    CHECK(parsed.trackNames.size() == 5 && parsed.trackNames[0] == "My Song Tempo" && parsed.trackNames[1] == "Piano" && parsed.trackNames[2] == "Bass" &&
          parsed.trackNames[4] == "Drums");
    CHECK(std::fabs(parsed.bpm - 120.0) < 0.01 && parsed.endBeat == 8.0);  // last note ends at beat 5 -> two bars
    for (const MidiNote& n : parsed.notes) {
        if (n.pitch == 36) CHECK(n.channel == kDrumChannel);
        if (n.pitch == 40) CHECK(n.program == 33 && n.channel != kDrumChannel && n.startBeat == 0.0);
        if (n.pitch == 64) CHECK(n.startBeat == 4.0);  // 2 s at 120 BPM, counted from the song start
    }
}

TEST(midi_export_keeps_the_instruments_for_a_gm_synth_after_import) {
    const std::vector<NoteEvent> notes = {note(40, 0.0, 1.0, 33), note(36, 0.5, 0.6, kDrumProgram), note(60, 1.0, 2.0, 0), note(64, 2.0, 2.5, 88)};
    const ParsedMidi parsed = parseMidiFile(buildMidi(notes, 120.0, "My Song"));
    CHECK(parsed.valid && parsed.notes.size() == 4);
    CHECK(parsed.hasGmSystemOn);  // GM reset in the tempo track: a GM synth switches to the GM sound set
    CHECK(parsed.instrumentNames.size() == 5 && parsed.instrumentNames[0].empty());
    CHECK(parsed.instrumentNames[1] == "Acoustic Grand Piano" && parsed.instrumentNames[2] == "Electric Bass (finger)" &&
          parsed.instrumentNames[3] == "Pad 1 (new age)" && parsed.instrumentNames[4] == "Standard Kit");
    CHECK(parsed.trackPrograms.size() == 5 && parsed.trackPrograms[0] == -1 && parsed.trackPrograms[1] == 0 && parsed.trackPrograms[2] == 33 &&
          parsed.trackPrograms[3] == 88 && parsed.trackPrograms[4] == 0);
    CHECK(parsed.trackBanks.size() == 5 && parsed.trackBanks[0] == -1);
    for (size_t i = 1; i < parsed.trackBanks.size(); ++i) CHECK(parsed.trackBanks[i] == 0);  // bank select before every program change
    for (const MidiNote& n : parsed.notes) {
        if (n.pitch == 40) CHECK(n.program == 33);
        if (n.pitch == 64) CHECK(n.program == 88);
    }
}

TEST(gm_sound_names_cover_every_program) {
    CHECK(gmSoundName(0) == "Acoustic Grand Piano" && gmSoundName(127) == "Gunshot" && gmSoundName(kDrumProgram) == "Standard Kit");
    for (int program = 0; program <= kMaxProgram; ++program) CHECK(!gmSoundName(program).empty());
    CHECK(gmSoundName(kNoProgram).empty() && gmSoundName(200).empty());
}

TEST(a_track_without_instrument_has_no_program_bank_or_instrument_name) {
    const std::vector<NoteEvent> notes = {note(60, 0.0, 1.0, kNoProgram)};
    const ParsedMidi parsed = parseMidiFile(buildMidi(notes, 120.0, "Plain"));
    CHECK(parsed.valid && parsed.notes.size() == 1);
    CHECK(parsed.trackPrograms.size() == 2 && parsed.trackPrograms[1] == -1 && parsed.trackBanks[1] == -1 && parsed.instrumentNames[1].empty());
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
    CHECK(tracks[15].channel == tracks[9].channel);
}

TEST(back_to_back_notes_of_one_key_all_survive_tick_rounding) {
    std::vector<MidiNote> notes;
    for (int i = 0; i < 50; ++i) notes.push_back({60, i * 0.3337, 0.3341, 100});  // each ends a hair after the next starts
    const ParsedMidi parsed = parseMidiFile(writeMidiFile(notes, 120.0, "Repeats"));
    CHECK(parsed.valid && parsed.notes.size() == notes.size());
    for (const MidiNote& n : parsed.notes) CHECK(n.lengthBeat > 0.3);
}

TEST(the_export_follows_mute_and_solo_unless_all_instruments_are_asked_for) {
    const std::vector<NoteEvent> notes = {note(40, 0, 1, 33), note(36, 0, 0.1, kDrumProgram), note(60, 0, 1, 0)};
    MixerState mix = MixerState{}.with(kDrumProgram, {true, false, 0.8f});
    CHECK(notesForExport(notes, mix, false).size() == 2 && notesForExport(notes, mix, true).size() == 3);
    mix = mix.with(33, {false, true, 0.8f});  // solo bass
    const std::vector<NoteEvent> soloed = notesForExport(notes, mix, false);
    CHECK(soloed.size() == 1 && soloed[0].program == 33);
    CHECK(midiFileName("/x/My Song.mp3", 120.0) == "My Song Tonetrace 120bpm.mid" && midiFileName("Recording", 97.5) == "Recording Tonetrace 97.5bpm.mid");
}

TEST(mixer_faders_mute_and_solo) {
    MixerState mix;
    CHECK(std::fabs(mix.gainOf(33) - 1.0f) < 1e-6f && mix.isAudible(33));  // default fader 0.8 = 0 dB
    mix = mix.with(33, {false, false, 0.0f});
    CHECK(mix.gainOf(33) == 0.0f);
    mix = mix.with(0, {false, true, 1.0f});
    CHECK(mix.isAudible(0) && !mix.isAudible(kDrumProgram) && std::fabs(mix.gainOf(0) - std::pow(10.0f, 6.0f / 20.0f)) < 1e-4f);
    const MixerState unchanged = mix;
    const MixerState other = mix.with(0, {true, true, 0.5f});
    CHECK(unchanged == mix && !(other == mix) && !other.isAudible(0));
    CHECK(MixerState{}.with(5, {false, false, NAN}).get(5).gain == kUnityFader);
}

TEST(song_files_are_recognised_and_bad_decodes_give_reasons) {
    CHECK(isSongFile("/a/b/Song.MP3") && isSongFile("x.flac") && isSongFile("y.aiff") && !isSongFile("notes.mid") && !isSongFile("wav"));
    CHECK(fileNameOf("/Volumes/Music/My Song.wav") == "My Song.wav");
    DecodedAudio audio;
    std::string why;
    CHECK(!decodeAudioFile("song.txt", &audio, &why) && why.find("unsupported") != std::string::npos);
    setAudioDecoder([](const std::string&, double rate, double, DecodedAudio* out, std::string*) {
        out->samples.clear();  // an empty file
        out->channels = 2;
        out->sampleRate = rate;
        return true;
    });
    CHECK(!decodeAudioFile("song.wav", &audio, &why) && why.find("no audio") != std::string::npos);
    setAudioDecoder([](const std::string&, double rate, double, DecodedAudio* out, std::string*) {
        out->samples = {100, 200, 300};  // not a whole number of stereo frames
        out->channels = 2;
        out->sampleRate = rate;
        return true;
    });
    CHECK(!decodeAudioFile("song.wav", &audio, &why));
    setAudioDecoder(nullptr);
    CHECK(!decodeAudioFile("song.wav", &audio, &why) && why.find("decoder") != std::string::npos);
}
