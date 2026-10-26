// Quick Grab: starting-point notes from the strongest lines of a spectrogram.
#pragma once
#include <vector>

#include "analysis/TaskControl.h"
#include "core/Types.h"

namespace ttr {

// amount 0..1: how far below the loudest line notes are still picked up (0 = strongest lines only).
// With `control`: progress is reported and a cancelled run returns an empty list.
std::vector<NoteEvent> quickGrabNotes(const Spectrogram& spectrogram, float amount, const FilterSettings& filter, TaskControl* control = nullptr);

}  // namespace ttr
