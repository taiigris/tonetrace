// Quick Grab: starting-point notes from the strongest lines of a spectrogram.
#pragma once
#include <string>
#include <vector>

#include "analysis/PitchLines.h"
#include "analysis/TaskControl.h"
#include "core/Types.h"

namespace ttr {

// Quick Grab only keeps segments of at least this length (no attack blips).
constexpr double kQuickGrabMinNoteSec = 0.120;

// amount 0..1: how far below the loudest line notes are still picked up (0 = strongest lines only).
// With `control`: progress is reported and a cancelled run returns an empty list.
std::vector<NoteEvent> quickGrabNotes(const Spectrogram& spectrogram, float amount, const FilterSettings& filter, TaskControl* control = nullptr);

// Quick Grab on the pitch lines: the strongest fundamental segments within the threshold of `amount` (overtones and,
// with Hide drums, drum hits are left out), placed on the rows of the tuned grid.
std::vector<NoteEvent> quickGrabFromLines(const PitchLines& lines, float amount, double tuningCents, const FilterSettings& filter);

// Quick Grab of a document: always the salience analysis of the spectrogram (as in v3), drum segments filtered out.
std::vector<NoteEvent> quickGrabSnapshot(const Snapshot& snapshot, float amount, const FilterSettings& filter, double tuningCents, TaskControl* control = nullptr);

enum class GrabEngine { Classic, Neural };

struct GrabOutcome {
    std::vector<NoteEvent> notes;
    GrabEngine used = GrabEngine::Classic;
    std::string fallbackReason;  // set when Neural was asked for but Classic ran
};

// Quick Grab with the chosen engine; Neural falls back to Classic (never fails) when the model or the audio is missing.
GrabOutcome quickGrabWithEngine(const Snapshot& snapshot, float amount, const FilterSettings& filter, double tuningCents, GrabEngine engine,
                                TaskControl* control = nullptr);

}  // namespace ttr
