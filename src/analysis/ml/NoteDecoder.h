// Model probabilities -> notes: hysteresis on the note activation, onset peaks start (and re-start) notes, short gaps of the
// same key are bridged unless a new onset follows, the pitch contour gives the deviation in cent.
#pragma once
#include <vector>

#include "analysis/ml/NeuralModel.h"

namespace ttr {

struct DecoderSettings {
    float onThreshold = 0.5f;     // a note starts at this activation (or at an onset peak with activation >= offThreshold)
    float offThreshold = 0.3f;    // and holds while it stays at or above this
    float onsetThreshold = 0.5f;  // onset peaks at or above this start a note
    float splitThreshold = 0.5f;  // onset peaks at or above this split a held note (repeated note) and stop a bridge
    int minFrames = 11;           // shorter notes are dropped (after bridging)
    int bridgeFrames = 0;         // gaps up to this many frames are bridged ...
    float bridgeFloor = 0.1f;     // ... when the activation in the gap stays at or above this
};

struct DecodedNote {
    int midi = 60;
    int first = 0, last = 0;  // inclusive frames
    float cents = 0.0f;       // contour deviation from the key centre, -50..+50
    float strength = 0.0f;    // mean activation
};

std::vector<DecodedNote> decodeNotes(const Activations& acts, const DecoderSettings& settings);

}  // namespace ttr
