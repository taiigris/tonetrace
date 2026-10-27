// Quick Grab 4.4: long, held notes instead of fragments. Salience with harmonic cancellation (bass fundamental from long
// windows), pump compensation per pitch, thresholds relative to the octave band, hysteresis, drum frames as "unknown"
// (bridged, never evidence), tempo-aware minimum length and grid rounding.
#pragma once
#include <vector>

#include "analysis/TaskControl.h"
#include "core/Types.h"

namespace ttr {

struct GrabTempo {
    double bpm = kDefaultBpm;
    double startPpq = 0.0;  // beat position of the recording start (the 1/16 grid is measured from it)
};

// `low`: frames * 24 bass energies of the long windows (see LowBand.h), empty = use the raw spectrogram there.
// amount 0..1 as the Quick Grab parameter. Returns no notes when cancelled.
std::vector<NoteEvent> sustainGrabNotes(const Spectrogram& spec, const std::vector<float>& low, float amount, const GrabTempo& tempo, TaskControl* control = nullptr);

// Shortest note: a 1/16 at the tempo (fallback 120 ms for a missing or odd tempo).
double minNoteSecForTempo(double bpm);

}  // namespace ttr
