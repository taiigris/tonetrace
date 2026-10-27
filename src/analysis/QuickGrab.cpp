#include "analysis/QuickGrab.h"

#include <algorithm>
#include <cmath>

#include "analysis/NoteAnalyzer.h"

namespace ttr {

std::vector<NoteEvent> quickGrabNotes(const Spectrogram& spectrogram, float amount, const FilterSettings& filter, TaskControl* control) {
    const std::shared_ptr<Salience> salience = computeSalience(spectrogram, filter, control);
    if (!salience || (control && control->cancelled())) return {};
    std::vector<NoteEvent> notes = extractNotes(*salience, amount, spectrogram, filter, kQuickGrabMinNoteSec);
    if (control) control->report(1.0f);
    for (NoteEvent& n : notes) { n.strength = 1.0f; n.selected = false; }
    return notes;
}

std::vector<NoteEvent> quickGrabFromLines(const PitchLines& lines, float amount, double tuningCents, const FilterSettings& filter) {
    constexpr double kMinGrabSec = 0.06;
    std::vector<NoteEvent> notes;
    const float threshold = lines.peak * std::pow(10.0f, sensitivityToThresholdDb(amount) / 20.0f);
    for (const LineTrack& t : lines.tracks) {
        if (t.flags & (kLineHidden | kLineHarmonic)) continue;
        if (filter.hideDrums && (t.flags & kLineDrum)) continue;
        if (t.peakLevel < threshold || t.endSec - t.startSec < kMinGrabSec) continue;
        NoteEvent n;
        n.pitch = std::clamp(rowOfMidi(t.meanMidi, tuningCents), kMinNote, kMaxNote);
        n.startSec = std::max(0.0, t.startSec);
        n.endSec = t.endSec;
        notes.push_back(n);
    }
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.startSec != b.startSec ? a.startSec < b.startSec : a.pitch < b.pitch;
    });
    return notes;
}

std::vector<NoteEvent> quickGrabSnapshot(const Snapshot& snapshot, float amount, const FilterSettings& filter, double tuningCents, TaskControl* control) {
    // 4.3: the proven salience analysis on the harmonic part always decides (the display toggle for drums only dims the picture).
    // The fine pitch of the lines (tuning) only shifts the display grid; it plays no part in the segmentation.
    if (!snapshot.spectrogram) return {};
    FilterSettings analysis = filter;
    analysis.hideDrums = true;
    return quickGrabNotes(*snapshot.spectrogram, amount, analysis, control);
}

}  // namespace ttr
