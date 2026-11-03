// 4.5: neural engine pieces that run without CoreML: resampler, decoder, windowing, Tonetrace rules, fallback to Classic.
#include <algorithm>
#include <cmath>
#include <memory>

#include "TestUtil.h"
#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"
#include "analysis/ml/NeuralEngine.h"
#include "analysis/ml/NeuralModel.h"
#include "analysis/ml/NoteDecoder.h"
#include "analysis/ml/Resampler.h"
#include "state/UiSettings.h"

using namespace ttr;

namespace {

std::vector<float> sine(double hz, double rate, double seconds, float amp) {
    std::vector<float> s(size_t(rate * seconds));
    for (size_t i = 0; i < s.size(); ++i) s[i] = amp * float(std::sin(2.0 * M_PI * hz * double(i) / rate));
    return s;
}

// Frequency from the zero crossings and RMS of the middle half (no edge effects).
void measure(const std::vector<float>& s, double rate, double* hz, double* rms) {
    const size_t a = s.size() / 4, b = 3 * s.size() / 4;
    int crossings = 0;
    double sum = 0.0;
    for (size_t i = a; i < b; ++i) {
        sum += double(s[i]) * s[i];
        if (i > a && (s[i - 1] < 0.0f) != (s[i] < 0.0f)) ++crossings;
    }
    *hz = crossings / 2.0 / (double(b - a) / rate);
    *rms = std::sqrt(sum / double(b - a));
}

Activations emptyActs(int frames) {
    Activations a;
    a.frames = frames;
    a.note.assign(size_t(frames) * kNeuralKeys, 0.0f);
    a.onset.assign(a.note.size(), 0.0f);
    a.contour.assign(size_t(frames) * kNeuralContourBins, 0.0f);
    return a;
}

void setNote(Activations& a, int midi, int first, int last, float v) {
    for (int t = first; t <= last; ++t) a.note[size_t(t) * kNeuralKeys + size_t(midi - kNeuralLowestMidi)] = v;
}
void setOnset(Activations& a, int midi, int frame, float v) { a.onset[size_t(frame) * kNeuralKeys + size_t(midi - kNeuralLowestMidi)] = v; }

// Marks key 0 active in every output frame whose 256 input samples are loud: checks the window and overlap bookkeeping.
class EnvelopeModel final : public NeuralModel {
public:
    int calls = 0;
    bool infer(const float* window, float* note, float* onset, float* contour) override {
        ++calls;
        std::fill_n(note, kNeuralWindowFrames * kNeuralKeys, 0.0f);
        std::fill_n(onset, kNeuralWindowFrames * kNeuralKeys, 0.0f);
        std::fill_n(contour, kNeuralWindowFrames * kNeuralContourBins, 0.0f);
        for (int f = 0; f < kNeuralWindowFrames; ++f) {
            double sum = 0.0;
            for (int i = 0; i < kNeuralHop && f * kNeuralHop + i < kNeuralWindowSamples; ++i) sum += std::abs(window[f * kNeuralHop + i]);
            note[size_t(f) * kNeuralKeys] = sum / kNeuralHop > 0.1 ? 1.0f : 0.0f;
        }
        return true;
    }
};

class FailingModel final : public NeuralModel {
public:
    bool infer(const float*, float*, float*, float*) override { return false; }
};

}  // namespace

TEST(resampler_keeps_frequency_and_level) {
    for (const double from : {48000.0, 24000.0, 44100.0}) {
        const std::vector<float> in = sine(440.0, from, 1.0, 0.5f);
        const std::vector<float> out = resampleMono(in.data(), in.size(), from, kNeuralRate);
        double hz = 0.0, rms = 0.0;
        measure(out, kNeuralRate, &hz, &rms);
        CHECK_MSG(std::abs(double(out.size()) - kNeuralRate) <= 2.0, "%zu samples from %.0f Hz", out.size(), from);
        CHECK_MSG(std::abs(hz - 440.0) < 2.0, "%.1f Hz from %.0f Hz", hz, from);
        CHECK_MSG(std::abs(rms - 0.5 / std::sqrt(2.0)) < 0.01, "rms %.4f from %.0f Hz", rms, from);
    }
}

TEST(resampler_removes_content_above_the_new_nyquist) {
    const std::vector<float> in = sine(15000.0, 48000.0, 1.0, 0.5f);
    const std::vector<float> out = resampleMono(in.data(), in.size(), 48000.0, kNeuralRate);
    double hz = 0.0, rms = 0.0;
    measure(out, kNeuralRate, &hz, &rms);
    CHECK_MSG(rms < 0.01, "rms %.4f", rms);
}

TEST(decoder_turns_a_held_activation_into_one_note) {
    Activations a = emptyActs(200);
    setNote(a, 41, 20, 149, 0.8f);
    setOnset(a, 41, 20, 0.9f);
    const std::vector<DecodedNote> notes = decodeNotes(a, DecoderSettings{});
    CHECK(notes.size() == 1);
    if (notes.size() == 1) CHECK(notes[0].midi == 41 && notes[0].first == 20 && notes[0].last == 149 && std::abs(notes[0].strength - 0.8f) < 1e-4f);
}

TEST(decoder_splits_a_repeated_note_at_its_onset) {
    Activations a = emptyActs(200);
    setNote(a, 36, 20, 149, 0.8f);
    setOnset(a, 36, 20, 0.9f);
    setOnset(a, 36, 80, 0.9f);
    const std::vector<DecodedNote> notes = decodeNotes(a, DecoderSettings{});
    CHECK(notes.size() == 2);
    if (notes.size() == 2) CHECK(notes[0].last == 79 && notes[1].first == 80 && notes[1].last == 149);
}

TEST(decoder_bridges_a_short_gap_but_not_a_new_attack) {
    DecoderSettings s;
    s.bridgeFrames = 6;
    s.bridgeFloor = 0.1f;
    Activations a = emptyActs(200);
    setNote(a, 29, 20, 70, 0.8f);
    setNote(a, 29, 71, 75, 0.2f);  // sidechain dip below the hold threshold
    setNote(a, 29, 76, 150, 0.8f);
    std::vector<DecodedNote> notes = decodeNotes(a, s);
    CHECK(notes.size() == 1);
    if (notes.size() == 1) CHECK(notes[0].first == 20 && notes[0].last == 150);
    setOnset(a, 29, 76, 0.9f);  // the same gap with a new attack after it: two notes
    notes = decodeNotes(a, s);
    CHECK(notes.size() == 2);
}

TEST(decoder_drops_short_notes_and_reads_the_contour) {
    Activations a = emptyActs(100);
    setNote(a, 60, 10, 15, 0.9f);  // 6 frames < 11
    setNote(a, 64, 30, 60, 0.9f);
    const int centre = 3 * (64 - kNeuralLowestMidi) + 1;
    for (int t = 30; t <= 60; ++t) a.contour[size_t(t) * kNeuralContourBins + size_t(centre + 1)] = 1.0f;  // one bin sharp
    const std::vector<DecodedNote> notes = decodeNotes(a, DecoderSettings{});
    CHECK(notes.size() == 1);
    if (notes.size() == 1) CHECK(notes[0].midi == 64 && std::abs(notes[0].cents - 100.0f / 3.0f) < 0.1f);
}

TEST(transcribe_keeps_the_model_frames_aligned_with_the_audio) {
    std::vector<float> audio(size_t(9.0 * kNeuralRate), 0.0f);
    for (size_t i = size_t(3.0 * kNeuralRate); i < size_t(8.5 * kNeuralRate); ++i) audio[i] = 0.5f;  // spans windows 2 to 5
    EnvelopeModel model;
    Activations acts;
    CHECK(transcribe(model, audio, &acts));
    CHECK(model.calls == 6);  // 9 s plus half an overlap in front, a window every 36164 samples
    CHECK(acts.frames == int(std::ceil(9.0 * kNeuralRate / kNeuralHop)));
    int first = -1, last = -1;
    for (int t = 0; t < acts.frames; ++t)
        if (acts.noteAt(t, 0) > 0.5f) { if (first < 0) first = t; last = t; }
    CHECK_MSG(std::abs(first - int(3.0 * kNeuralFps)) <= 2 && std::abs(last - int(8.5 * kNeuralFps)) <= 2, "active frames %d..%d", first, last);
}

TEST(neural_notes_follow_tempo_and_leave_the_kick_out) {
    const double bpm = 120.0;  // beat 0.5 s
    Activations a = emptyActs(int(4.0 * kNeuralFps));
    const int beat = int(std::lround(0.5 * kNeuralFps));
    setNote(a, 33, beat, beat + 10, 0.9f);                 // A1, 0.12 s on the beat: kick
    setNote(a, 41, 2 * beat + 5, 2 * beat + 60, 0.9f);    // F2, 0.65 s: a note
    setNote(a, 60, 3 * beat + 20, 3 * beat + 26, 0.9f);   // 7 frames < 1/16 at 120 BPM (11 frames)
    BeatGrid grid;
    grid.beatSec = 0.5;
    grid.firstBeatSec = 0.0;
    const std::vector<NoteEvent> notes = neuralNotes(a, 0.5f, bpm, grid);
    CHECK(notes.size() == 1);
    if (notes.size() == 1) CHECK(notes[0].pitch == 41);
    CHECK(neuralNotes(a, 0.5f, bpm, BeatGrid{}).size() == 2);  // no grid: the low note stays
}

TEST(quick_grab_slider_maps_to_the_activation_threshold) {
    CHECK(std::abs(neuralDecoderSettings(0.0f, 120.0).onThreshold - kNeuralOnAtZero) < 1e-6f);
    CHECK(std::abs(neuralDecoderSettings(1.0f, 120.0).onThreshold - kNeuralOnAtFull) < 1e-6f);
    CHECK(neuralDecoderSettings(0.5f, 120.0).minFrames == 11);  // 1/16 at 120 BPM = 0.125 s
}

TEST(neural_quick_grab_falls_back_to_classic_without_a_model) {
    const std::vector<float> audio = sine(220.0, 44100.0, 2.0, 0.5f);
    Snapshot snap;
    snap.spectrogram = computeSpectrogram(audio.data(), audio.size(), 44100.0);
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = 44100.0;
    for (float v : audio) clip->samples.push_back(int16_t(std::lround(v * 32767.0f)));
    snap.audio = clip;
    const std::vector<NoteEvent> classic = quickGrabSnapshot(snap, 0.5f, FilterSettings{}, 0.0);
    for (const std::shared_ptr<NeuralModel>& model : {std::shared_ptr<NeuralModel>(), std::shared_ptr<NeuralModel>(std::make_shared<FailingModel>())}) {
        setNeuralModel(model);
        const GrabOutcome out = quickGrabWithEngine(snap, 0.5f, FilterSettings{}, 0.0, GrabEngine::Neural);
        CHECK(out.used == GrabEngine::Classic && !out.fallbackReason.empty());
        CHECK(out.notes.size() == classic.size());
    }
    Snapshot noAudio = snap;
    noAudio.audio = nullptr;
    setNeuralModel(std::make_shared<EnvelopeModel>());
    const GrabOutcome out = quickGrabWithEngine(noAudio, 0.5f, FilterSettings{}, 0.0, GrabEngine::Neural);
    CHECK(out.used == GrabEngine::Classic && out.fallbackReason.find("not stored") != std::string::npos);
    setNeuralModel(nullptr);
}

TEST(editor_state_keeps_the_engine_and_older_states_load_classic) {
    UiSettings s;
    s.grabEngine = int(GrabEngine::Neural);
    const std::vector<uint8_t> bytes = encodeUiSettings(s);
    UiSettings back;
    CHECK(decodeUiSettings(bytes.data(), bytes.size(), &back) && back.grabEngine == int(GrabEngine::Neural) && back == s);
    CHECK(UiSettings{}.grabEngine == int(GrabEngine::Classic));
}
