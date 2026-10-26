// Drum filter: transient gate on the harmonic spectrum and the per-segment note filter.
#pragma once
#include <vector>

#include "analysis/NoteAnalyzer.h"
#include "core/Types.h"

namespace ttr {

// Harmonic note energies (frames * kNumNotes) with frames around percussive onsets attenuated.
// drumFilter 0..1 scales how eagerly the gate fires.
std::vector<float> gatedHarmonicEnergies(const Spectrogram& spec, float drumFilter);

// Computes note.tonality and note.sustain from the spectrogram.
void annotateSegment(const Spectrogram& spec, NoteSegment* segment);

enum class DrumReason { None, LowTonality, ShortDecay, BlipAtOnset, DownwardGlide, DrumTail };

// Why the (annotated) segment looks like a drum hit rather than a held tonal note; None = keep it.
DrumReason drumReason(const Spectrogram& spec, const NoteSegment& segment, float drumFilter);
inline bool isDrumSegment(const Spectrogram& spec, const NoteSegment& segment, float drumFilter) {
    return drumReason(spec, segment, drumFilter) != DrumReason::None;
}

}  // namespace ttr
