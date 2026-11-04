// Builders for the immutable document Snapshot (recording, notes, key).
#pragma once
#include <vector>

#include "core/Types.h"

namespace ttr {

struct Recording {
    std::shared_ptr<const Spectrogram> spectrogram;
    std::shared_ptr<const WavePeaks> peaks;
    std::shared_ptr<const AudioClip> audio;
    std::shared_ptr<const PitchLines> lines;
    std::array<float, 12> chroma{};  // see spectrogramChroma; stored so the key needs no re-analysis
    double lengthSec = 0.0;
    double bpm = kDefaultBpm;
    double startPpq = 0.0;
    std::string sourceName;  // imported song file name, empty for a recording
};

// Pitch-class energy of the recording from the harmonic-suppressed salience (so overtones do not
// turn every note into a major chord); zero without a spectrogram. Takes a moment for long recordings.
std::array<float, 12> spectrogramChroma(const Spectrogram& spectrogram);

// Key of the drawn notes; falls back to the recording's chroma while no notes exist.
KeyResult keyOf(const std::vector<NoteEvent>& notes, const std::array<float, 12>& chroma);

SnapshotPtr makeSnapshot(std::vector<NoteEvent> notes, const Recording& recording, uint64_t version);
// Same recording, other notes.
SnapshotPtr withNotes(const Snapshot& old, std::vector<NoteEvent> notes);
// Same document with computed pitch lines (restored state).
SnapshotPtr withLines(const Snapshot& old, std::shared_ptr<const PitchLines> lines);
// Same notes, other recording (new recording or restored state).
SnapshotPtr withRecording(const Snapshot* old, const Recording& recording);

}  // namespace ttr
