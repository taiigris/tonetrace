// Quick Grab with the neural engine: model over the stored recording, NoteDecoder, then the Tonetrace rules
// (minimum length 1/16 note and gap bridging up to 1/8 note at the host tempo, no short low notes on the kick).
#pragma once
#include <string>
#include <vector>

#include "analysis/BeatHits.h"
#include "analysis/ml/NoteDecoder.h"
#include "core/Types.h"

namespace ttr {

// Tuned on the synthetic mixes and the reference window (docs/ACCURACY.md, 4.5), no track-specific values.
constexpr float kNeuralOnAtZero = 0.6f;         // activation threshold at Quick Grab 0 % ...
constexpr float kNeuralOnAtFull = 0.35f;        // ... and at 100 %
constexpr float kNeuralOffShare = 0.45f;        // hold threshold = start threshold * this
constexpr float kNeuralSplitThreshold = 0.85f;  // only a clear new attack splits a held note (vibrato and pumping do not)
constexpr int kKickMaxMidi = 40;          // E2: below this, short notes starting on a beat are the kick

struct NeuralGrab {
    bool ok = false;
    std::string reason;  // why the engine could not run (the caller falls back to Classic)
    std::vector<NoteEvent> notes;
};

// Decoder settings for a Quick Grab amount (0..1) and host tempo.
DecoderSettings neuralDecoderSettings(float amount, double bpm);
// Notes from model activations (frames at kNeuralFps); `grid` may be invalid (no kick filter then).
std::vector<NoteEvent> neuralNotes(const Activations& acts, float amount, double bpm, const BeatGrid& grid);
// The whole path for a document; ok = false when there is no model, no stored audio or the model fails.
NeuralGrab neuralGrabSnapshot(const Snapshot& snapshot, float amount, const FilterSettings& filter, TaskControl* control);
// Same, for a mono signal at any rate (accuracy tool).
NeuralGrab neuralGrabAudio(const std::vector<float>& audio, double rate, const Spectrogram* spec, double bpm, double startPpq, float amount,
                           const FilterSettings& filter, TaskControl* control);

}  // namespace ttr
