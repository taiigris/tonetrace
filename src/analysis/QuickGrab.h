// Quick Grab: starting-point notes from the strongest lines of a spectrogram.
#pragma once
#include <vector>

#include "analysis/PitchLines.h"
#include "analysis/TaskControl.h"
#include "core/Types.h"

namespace ttr {

// amount 0..1: how far below the loudest line notes are still picked up (0 = strongest lines only).
// With `control`: progress is reported and a cancelled run returns an empty list.
std::vector<NoteEvent> quickGrabNotes(const Spectrogram& spectrogram, float amount, const FilterSettings& filter, TaskControl* control = nullptr);

// Quick Grab on the pitch lines: the strongest fundamental segments within the threshold of `amount` (overtones and,
// with Hide drums, drum hits are left out), placed on the rows of the tuned grid.
std::vector<NoteEvent> quickGrabFromLines(const PitchLines& lines, float amount, double tuningCents, const FilterSettings& filter);

// Quick Grab of a document: the pitch lines when the snapshot has them, else the salience of the spectrogram.
std::vector<NoteEvent> quickGrabSnapshot(const Snapshot& snapshot, float amount, const FilterSettings& filter, double tuningCents, TaskControl* control = nullptr);

}  // namespace ttr
