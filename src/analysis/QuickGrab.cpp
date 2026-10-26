#include "analysis/QuickGrab.h"

#include "analysis/NoteAnalyzer.h"

namespace ttr {

std::vector<NoteEvent> quickGrabNotes(const Spectrogram& spectrogram, float amount, const FilterSettings& filter, TaskControl* control) {
    const std::shared_ptr<Salience> salience = computeSalience(spectrogram, filter, control);
    if (!salience || (control && control->cancelled())) return {};
    std::vector<NoteEvent> notes = extractNotes(*salience, amount, spectrogram, filter);
    if (control) control->report(1.0f);
    for (NoteEvent& n : notes) { n.strength = 1.0f; n.selected = false; }
    return notes;
}

}  // namespace ttr
