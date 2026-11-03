#include "analysis/ml/NeuralEngine.h"

#include <algorithm>
#include <cmath>

#include "analysis/ml/Resampler.h"

namespace ttr {

namespace {

constexpr double kSecondsPerMinute = 60.0;
constexpr float kBridgeFloorShare = 0.5f;  // bridged gaps keep at least half the hold threshold

double beatOf(double bpm) { return kSecondsPerMinute / (bpm > 0.0 && std::isfinite(bpm) ? bpm : kDefaultBpm); }

bool startsOnBeat(double startSec, const BeatGrid& grid) {
    if (!grid.isValid()) return false;
    const double phase = std::fmod(startSec - grid.firstBeatSec, grid.beatSec);
    const double distance = std::min(std::abs(phase), grid.beatSec - std::abs(phase));
    return distance <= kBeatToleranceSec;
}

}  // namespace

DecoderSettings neuralDecoderSettings(float amount, double bpm) {
    const float a = std::clamp(std::isfinite(amount) ? amount : 0.0f, 0.0f, 1.0f);
    DecoderSettings s;
    s.onThreshold = kNeuralOnAtZero + (kNeuralOnAtFull - kNeuralOnAtZero) * a;
    s.offThreshold = s.onThreshold * kNeuralOffShare;
    s.onsetThreshold = 0.5f;
    s.splitThreshold = kNeuralSplitThreshold;
    const double beat = beatOf(bpm);
    s.minFrames = std::max(1, int(std::ceil(beat / 4.0 * kNeuralFps)));  // 1/16 note
    s.bridgeFrames = int(std::floor(beat / 2.0 * kNeuralFps));           // 1/8 note
    s.bridgeFloor = s.offThreshold * kBridgeFloorShare;
    return s;
}

std::vector<NoteEvent> neuralNotes(const Activations& acts, float amount, double bpm, const BeatGrid& grid) {
    const double beat = beatOf(bpm);
    std::vector<NoteEvent> notes;
    for (const DecodedNote& d : decodeNotes(acts, neuralDecoderSettings(amount, bpm))) {
        if (d.midi < kMinNote || d.midi > kMaxNote) continue;  // outside the note map
        NoteEvent n;
        n.pitch = d.midi;
        n.startSec = double(d.first) / kNeuralFps;
        n.endSec = double(d.last + 1) / kNeuralFps;
        n.strength = 1.0f;
        n.selected = false;
        const bool isKick = d.midi < kKickMaxMidi && n.endSec - n.startSec < beat / 2.0 && startsOnBeat(n.startSec, grid);
        if (!isKick) notes.push_back(n);
    }
    return notes;
}

NeuralGrab neuralGrabAudio(const std::vector<float>& audio, double rate, const Spectrogram* spec, double bpm, double startPpq, float amount,
                           const FilterSettings& filter, TaskControl* control) {
    NeuralGrab result;
    const std::shared_ptr<NeuralModel> model = neuralModel(&result.reason);
    if (!model) return result;
    if (audio.empty() || rate <= 0.0) { result.reason = "no audio"; return result; }
    const std::vector<float> input = resampleMono(audio.data(), audio.size(), rate, kNeuralRate);
    Activations acts;
    if (!transcribe(*model, input, &acts, control)) {
        result.reason = control && control->cancelled() ? "cancelled" : "the model failed";
        return result;
    }
    const BeatGrid grid = spec ? findBeatGrid(*spec, bpm, startPpq, filter.drumFilter) : BeatGrid{};
    result.notes = neuralNotes(acts, amount, bpm, grid);
    result.ok = true;
    if (control) control->report(1.0f);
    return result;
}

NeuralGrab neuralGrabSnapshot(const Snapshot& snapshot, float amount, const FilterSettings& filter, TaskControl* control) {
    if (!snapshot.audio || snapshot.audio->samples.empty()) {
        NeuralGrab none;
        none.reason = "the recording audio is not stored (longer than 60 s)";
        return none;
    }
    const AudioClip& clip = *snapshot.audio;
    const std::vector<float> audio = resampleInt16(clip.samples.data(), clip.samples.size(), clip.sampleRate, clip.sampleRate);
    return neuralGrabAudio(audio, clip.sampleRate, snapshot.spectrogram.get(), snapshot.bpm, snapshot.startPpq, amount, filter, control);
}

}  // namespace ttr
