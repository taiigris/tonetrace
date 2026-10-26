#include <cmath>

#include "TestSignals.h"
#include "TestUtil.h"
#include "analysis/Spectrogram.h"
#include "analysis/WavePeaks.h"
#include "core/NoteEdit.h"
#include "core/Snapshot.h"
#include "midi/ExportPlanner.h"
#include "midi/MidiFileWriter.h"
#include "state/StateCodec.h"

using namespace ttr;

TEST(midi_roundtrip_notes_tempo_and_header) {
    const std::vector<MidiNote> in = {{57, 0.0, 2.0, 100}, {61, 0.5, 0.25, 64}, {64, 1.0, 0.5, 127}, {40, 3.75, 1.0, 1}};
    const auto bytes = writeMidiFile(in, 93.5, "Tonetrace A minor");
    const ParsedMidi out = parseMidiFile(bytes);
    CHECK(out.valid);
    CHECK(out.format == 1 && out.tracks == 2 && out.ppq == 960);
    CHECK(std::fabs(out.bpm - 93.5) < 0.01);
    CHECK(out.trackName == "Tonetrace A minor");
    CHECK(out.notes.size() == in.size());
    for (const MidiNote& n : in) {
        bool found = false;
        for (const MidiNote& o : out.notes)
            found |= o.pitch == n.pitch && std::fabs(o.startBeat - n.startBeat) < 1e-3 && std::fabs(o.lengthBeat - n.lengthBeat) < 1e-3 && o.velocity == n.velocity;
        CHECK_MSG(found, "note %d lost", n.pitch);
    }
}

TEST(midi_parser_rejects_garbage) {
    CHECK(!parseMidiFile({1, 2, 3}).valid);
    CHECK(!parseMidiFile(std::vector<uint8_t>(64, 0)).valid);
}

TEST(export_plan_beats_origin_snap_and_fixed_velocity) {
    TimeGrid g;
    g.bpm = 120.0;  // 1 s = 2 beats
    g.snapMode = 0;
    NoteEvent a, b;
    a.pitch = 57; a.startSec = 1.1; a.endSec = 2.1; a.selected = true;
    b.pitch = 61; b.startSec = 1.6; b.endSec = 2.1; b.strength = 0.1f; b.selected = true;
    const auto raw = planExport({a, b}, g);
    CHECK(raw.size() == 2);
    CHECK(std::fabs(raw[0].startBeat - 0.2) < 1e-9);  // origin floor(2.2) = 2 beats
    CHECK(std::fabs(raw[0].lengthBeat - 2.0) < 1e-9);
    CHECK(raw[0].velocity == kExportVelocity && raw[1].velocity == kExportVelocity);  // drawn notes carry no dynamics
    g.snapMode = 2;  // 1/8 = 0.5 beat
    const auto q = planExport({a, b}, g);
    CHECK(std::fabs(q[0].startBeat - 0.0) < 1e-9 && std::fabs(q[1].startBeat - 1.0) < 1e-9 && q[1].lengthBeat >= 0.5);
    g.snapMode = 0;
    g.startPpq = 8.0;  // project beats: the position in the project does not move the clip start
    CHECK(std::fabs(planExport({a}, g)[0].startBeat - 0.2) < 1e-9);
    CHECK(planExport({}, g).empty());
}

TEST(export_of_drawn_notes_roundtrips_through_the_midi_parser) {
    TimeGrid grid;
    grid.bpm = 100.0;
    grid.snapMode = 3;
    std::vector<NoteEvent> notes;
    const double beat = 60.0 / grid.bpm;
    for (int i = 0; i < 4; ++i) notes.push_back(makeNote(60 + 3 * i, 0.5 * i, 0.5 * i + 0.4, grid, true));
    notes = withSelection(notes, {1, 2}, SelectMode::Replace);
    Recording rec;
    rec.bpm = grid.bpm;
    const auto snap = makeSnapshot(notes, rec, 1);

    const auto parsed = parseMidiFile(buildMidiForNotes(*snap, 3, KeyResult{9, true, 0.9f}));  // selected notes only
    CHECK(parsed.valid && parsed.notes.size() == 2 && std::fabs(parsed.bpm - 100.0) < 0.01);
    CHECK(parsed.trackName == "Tonetrace A minor");
    for (const MidiNote& n : parsed.notes) CHECK(n.velocity == 100 && (n.pitch == 63 || n.pitch == 66));
    CHECK(std::fabs(parsed.notes[1].startBeat - parsed.notes[0].startBeat - 0.5 / beat) < 0.26);

    const auto all = parseMidiFile(buildMidiForNotes(*makeSnapshot(withAllSelected(notes, false), rec, 2), 3, KeyResult{}));
    CHECK(all.valid && all.notes.size() == 4);  // nothing selected: every note is exported
    CHECK(parsed.notes.front().startBeat < 0.99);
}

TEST(midi_file_name_and_clip_length_in_whole_bars) {
    CHECK(midiFileName(KeyResult{9, true, 0.9f}, 120.0) == "Tonetrace A minor 120bpm.mid");
    CHECK(midiFileName(KeyResult{0, false, 0.9f}, 93.5) == "Tonetrace C major 93.5bpm.mid");
    CHECK(midiFileName(KeyResult{}, 100.02) == "Tonetrace Unknown 100bpm.mid");
    CHECK(midiTrackName(KeyResult{1, true, 0.9f}) == "Tonetrace C# minor");

    CHECK(clipLengthBeats({}) == 4.0);                                  // at least one bar
    CHECK(clipLengthBeats({{60, 0.0, 3.9, 100}}) == 4.0);
    CHECK(clipLengthBeats({{60, 0.0, 4.0, 100}}) == 4.0);               // exactly one bar stays one bar
    CHECK(clipLengthBeats({{60, 0.0, 4.2, 100}}) == 8.0);               // rounded up
    CHECK(clipLengthBeats({{60, 5.0, 1.0, 100}, {62, 11.5, 1.0, 100}}) == 16.0);

    TimeGrid grid;
    grid.bpm = 120.0;
    grid.snapMode = 3;
    Recording rec;
    rec.bpm = 120.0;
    const auto notes = withNoteAdded(withNoteAdded({}, makeNote(57, 0.0, 2.5, grid, true)), makeNote(64, 4.0, 4.6, grid, true));
    const auto snap = makeSnapshot(notes, rec, 1);
    const ParsedMidi parsed = parseMidiFile(buildMidiForNotes(*snap, 3, KeyResult{9, true, 0.9f}));
    CHECK(parsed.valid && parsed.notes.size() == 2);
    CHECK(std::fabs(parsed.endBeat - 12.0) < 1e-6);  // last note ends at beat ~9.2 -> 3 bars
    CHECK(std::fmod(parsed.endBeat, kBeatsPerBar) == 0.0);
    double lastOff = 0.0;
    for (const MidiNote& n : parsed.notes) lastOff = std::max(lastOff, n.startBeat + n.lengthBeat);
    CHECK(parsed.endBeat >= lastOff - 1e-9);
}

TEST(state_codec_roundtrip_with_spectrogram_peaks_notes_and_audio) {
    constexpr double kRate = 44100.0;
    const auto sig = tsig::synth(tsig::chordThenMelody(), kRate, 6.5, false);
    Recording rec;
    rec.spectrogram = computeSpectrogram(sig.data(), sig.size(), kRate);
    PeakBuilder peaks;
    peaks.prepare(kRate, 7.0);
    peaks.advance(sig.data(), sig.size());
    rec.peaks = peaks.finish();
    rec.chroma = spectrogramChroma(*rec.spectrogram);
    auto clip = std::make_shared<AudioClip>();
    for (int i = 0; i < 22050; ++i) clip->samples.push_back(int16_t((i % 100) * 100 - 5000));
    rec.audio = clip;
    rec.lengthSec = 6.5;
    rec.bpm = 97.0;
    rec.startPpq = 12.5;
    TimeGrid grid;
    auto notes = withNoteAdded({}, makeNote(57, 0.0, 2.0, grid, true));
    notes = withNoteAdded(notes, makeNote(64, 2.5, 3.0, grid, true));
    notes = withSelection(notes, {1}, SelectMode::Replace);
    const auto snap = makeSnapshot(notes, rec, 7);

    ParamValues params = defaultParams();
    params[kParamGain] = 0.25f;
    params[kParamSnap] = 2.0f;
    params[kParamSolo] = 1.0f;
    params[kParamRecord] = 1.0f;   // must never be restored
    params[kParamPreview] = 1.0f;  // neither
    const auto bytes = encodeState(params, snap.get());
    CHECK(bytes.size() < 6u * 1024u * 1024u);
    DecodedState out;
    CHECK(decodeState(bytes.data(), bytes.size(), &out));
    CHECK(out.params[kParamGain] == 0.25f && out.params[kParamSnap] == 2.0f && out.params[kParamSolo] == 1.0f);
    CHECK(out.params[kParamRecord] == 0.0f && out.params[kParamPreview] == 0.0f);
    const SnapshotPtr s = out.snapshot;
    CHECK(s && s->notes.size() == 2 && selectedCount(s->notes) == 1 && s->notes[1].selected);
    CHECK(s && std::fabs(s->notes[1].startSec - 2.5) < 1e-12 && s->notes[1].pitch == 64);
    CHECK(s && s->bpm == 97.0 && s->startPpq == 12.5 && s->version == 7);
    CHECK(s && s->key.root == snap->key.root && s->chroma == snap->chroma);
    CHECK(s && s->chroma[9] > 0.5f);  // A is strong in the chord + melody
    CHECK(s && s->spectrogram && s->spectrogram->frames == rec.spectrogram->frames);
    CHECK(s && s->spectrogram && s->spectrogram->harmonic.size() == rec.spectrogram->harmonic.size());
    if (s && s->spectrogram) {  // quantised to 60 dB: the strongest value survives, the content stays close
        CHECK(std::fabs(s->spectrogram->peak - rec.spectrogram->peak) < 1e-3f * rec.spectrogram->peak);
        const int f = int(2.2 / rec.spectrogram->hopSec), n = 61 - kMinNote;
        CHECK(std::fabs(s->spectrogram->rawAt(f, n) - rec.spectrogram->rawAt(f, n)) < 0.05f * rec.spectrogram->peak);
    }
    CHECK(s && s->peaks && s->peaks->count() == rec.peaks->count());
    if (s && s->peaks) CHECK(std::fabs(s->peaks->minMax[400] - rec.peaks->minMax[400]) < 0.01f);
    CHECK(s && s->audio && s->audio->samples == clip->samples && s->audio->sampleRate == clip->sampleRate);

    // optional parts: no audio, no recording at all
    Recording noAudio = rec;
    noAudio.audio = nullptr;
    const auto small = encodeState(params, makeSnapshot(notes, noAudio, 1).get());
    CHECK(small.size() + 40000 < bytes.size());
    DecodedState out2;
    CHECK(decodeState(small.data(), small.size(), &out2) && out2.snapshot && !out2.snapshot->audio && out2.snapshot->spectrogram);
    const auto notesOnly = encodeState(params, makeSnapshot(notes, Recording{}, 1).get());
    CHECK(decodeState(notesOnly.data(), notesOnly.size(), &out2) && out2.snapshot && !out2.snapshot->hasRecording() && out2.snapshot->notes.size() == 2);

    // corruption
    CHECK(!decodeState(bytes.data(), 10, &out));
    std::vector<uint8_t> bad = bytes;
    bad[0] ^= 0xFF;
    CHECK(!decodeState(bad.data(), bad.size(), &out));
    CHECK(!decodeState(bytes.data(), bytes.size() / 2, &out));
    CHECK(!decodeState(bytes.data(), bytes.size() - 1, &out));
    ParamValues onlyParams;
    CHECK(decodeStateParams(bytes.data(), bytes.size(), &onlyParams) && onlyParams[kParamGain] == 0.25f);
    const auto empty = encodeState(params, nullptr);
    CHECK(decodeState(empty.data(), empty.size(), &out) && !out.snapshot);
}
