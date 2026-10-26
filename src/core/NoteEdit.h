// Pure editing functions of the note model: grid, snap, magnet and the edits (draw, move, resize, delete,
// duplicate, select). Every function returns a new, sorted note list with same-pitch overlaps merged.
#pragma once
#include <vector>

#include "core/Types.h"

namespace ttr {

constexpr int kNumSnapModes = 5;
constexpr double kMinFreeNoteSec = 0.03;  // shortest note without snap
const char* snapName(int mode);           // Off, 1/4, 1/8, 1/16, 1/32
double snapGridBeats(int mode);           // 0 = off

struct TimeGrid {
    double bpm = kDefaultBpm;
    double startPpq = 0.0;  // project beat at time 0 of the recording
    int snapMode = 3;       // index into snapName
    double beatsPerSec() const { return (bpm > 0 ? bpm : kDefaultBpm) / 60.0; }
};

double gridStepSec(const TimeGrid& grid);            // 0 when snap is off
double snapTime(double sec, const TimeGrid& grid);   // nearest grid line; unchanged when snap is off
double minNoteSec(const TimeGrid& grid);

// Magnet: the pitch (within +-1 semitone of `pitch`) with the most energy between t0 and t1.
// `energies` is frames * kNumNotes; ties and silence keep `pitch`.
int magnetPitch(const float* energies, int frames, double hopSec, double t0, double t1, int pitch);

// A new note from a drag: ordered, snapped (when `snap`), at least minNoteSec long, starting at 0 or later.
NoteEvent makeNote(int pitch, double t0, double t1, const TimeGrid& grid, bool snap);

std::vector<NoteEvent> normalizedNotes(std::vector<NoteEvent> notes);  // merge same-pitch overlaps, sort
std::vector<NoteEvent> withNoteAdded(const std::vector<NoteEvent>& notes, const NoteEvent& added);
std::vector<NoteEvent> withMoved(const std::vector<NoteEvent>& notes, const std::vector<int>& indices, double dtSec, int dPitch);
std::vector<NoteEvent> withResized(const std::vector<NoteEvent>& notes, int index, bool resizeStart, double newTime, double minLen);
std::vector<NoteEvent> withDeleted(const std::vector<NoteEvent>& notes, const std::vector<int>& indices);
std::vector<NoteEvent> withSelectedDeleted(const std::vector<NoteEvent>& notes);
// Copies of the selected notes placed right behind the selection; the copies end up selected.
std::vector<NoteEvent> withDuplicated(const std::vector<NoteEvent>& notes);

enum class SelectMode { Replace, Add, Toggle };
std::vector<NoteEvent> withSelection(const std::vector<NoteEvent>& notes, const std::vector<int>& indices, SelectMode mode);
std::vector<NoteEvent> withAllSelected(const std::vector<NoteEvent>& notes, bool selected);

std::vector<int> selectedIndices(const std::vector<NoteEvent>& notes);
int selectedCount(const std::vector<NoteEvent>& notes);
// Notes touching the rectangle [t0,t1] x [pitchLow,pitchHigh] (lasso).
std::vector<int> indicesInRegion(const std::vector<NoteEvent>& notes, double t0, double t1, int pitchLow, int pitchHigh);

}  // namespace ttr
