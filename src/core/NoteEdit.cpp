#include "core/NoteEdit.h"

#include <algorithm>
#include <cmath>

namespace ttr {

const char* snapName(int mode) {
    static const char* names[kNumSnapModes] = {"Off", "1/4", "1/8", "1/16", "1/32"};
    return names[std::clamp(mode, 0, kNumSnapModes - 1)];
}

double snapGridBeats(int mode) {
    static const double grid[kNumSnapModes] = {0.0, 1.0, 0.5, 0.25, 0.125};
    return grid[std::clamp(mode, 0, kNumSnapModes - 1)];
}

double gridStepSec(const TimeGrid& g) { return snapGridBeats(g.snapMode) / g.beatsPerSec(); }

double snapTime(double sec, const TimeGrid& g) {
    const double step = snapGridBeats(g.snapMode);
    if (step <= 0.0) return sec;
    const double beat = g.startPpq + sec * g.beatsPerSec();
    return (std::round(beat / step) * step - g.startPpq) / g.beatsPerSec();
}

double minNoteSec(const TimeGrid& g) {
    const double step = gridStepSec(g);
    return step > 0.0 ? step : kMinFreeNoteSec;
}

int magnetPitch(const float* energies, int frames, double hopSec, double t0, double t1, int pitch) {
    if (!energies || frames <= 0 || hopSec <= 0.0) return pitch;
    if (t1 < t0) std::swap(t0, t1);
    const int f0 = std::clamp(int(std::floor(t0 / hopSec)), 0, frames - 1);
    const int f1 = std::clamp(std::max(f0, int(std::ceil(t1 / hopSec))), 0, frames - 1);
    const auto energyOf = [&](int p) {
        double sum = 0.0;
        for (int f = f0; f <= f1; ++f) sum += energies[size_t(f) * kNumNotes + size_t(p - kMinNote)];
        return sum;
    };
    int best = std::clamp(pitch, kMinNote, kMaxNote);
    double bestSum = energyOf(best);
    for (const int p : {pitch - 1, pitch + 1}) {  // a neighbour must be clearly stronger to win
        if (p < kMinNote || p > kMaxNote) continue;
        const double sum = energyOf(p);
        if (sum > bestSum * 1.000001 + 1e-12) { best = p; bestSum = sum; }
    }
    return best;
}

NoteEvent makeNote(int pitch, double t0, double t1, const TimeGrid& grid, bool snap) {
    if (t1 < t0) std::swap(t0, t1);
    if (snap) { t0 = snapTime(t0, grid); t1 = snapTime(t1, grid); }
    t0 = std::max(0.0, t0);
    const double minLen = snap ? minNoteSec(grid) : kMinFreeNoteSec;
    NoteEvent n;
    n.pitch = std::clamp(pitch, kMinNote, kMaxNote);
    n.startSec = t0;
    n.endSec = std::max(t1, t0 + minLen);
    return n;
}

std::vector<NoteEvent> normalizedNotes(std::vector<NoteEvent> notes) {
    std::sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.pitch != b.pitch ? a.pitch < b.pitch : a.startSec < b.startSec;
    });
    std::vector<NoteEvent> merged;
    for (const NoteEvent& n : notes) {
        if (!merged.empty() && merged.back().pitch == n.pitch && n.startSec < merged.back().endSec) {
            merged.back().endSec = std::max(merged.back().endSec, n.endSec);
            merged.back().selected = merged.back().selected || n.selected;
        } else {
            merged.push_back(n);
        }
    }
    std::sort(merged.begin(), merged.end(), [](const NoteEvent& a, const NoteEvent& b) {
        return a.startSec != b.startSec ? a.startSec < b.startSec : a.pitch < b.pitch;
    });
    return merged;
}

std::vector<NoteEvent> withNoteAdded(const std::vector<NoteEvent>& notes, const NoteEvent& added) {
    std::vector<NoteEvent> out = notes;
    out.push_back(added);
    return normalizedNotes(std::move(out));
}

std::vector<NoteEvent> withMoved(const std::vector<NoteEvent>& notes, const std::vector<int>& indices, double dtSec, int dPitch) {
    double dt = dtSec;
    int dp = dPitch;
    for (int i : indices) {
        if (i < 0 || i >= int(notes.size())) continue;
        dt = std::max(dt, -notes[size_t(i)].startSec);  // never before time 0
        dp = std::clamp(dp, kMinNote - notes[size_t(i)].pitch, kMaxNote - notes[size_t(i)].pitch);
    }
    std::vector<NoteEvent> out = notes;
    for (int i : indices) {
        if (i < 0 || i >= int(out.size())) continue;
        NoteEvent& n = out[size_t(i)];
        n.startSec += dt;
        n.endSec += dt;
        n.pitch += dp;
    }
    return normalizedNotes(std::move(out));
}

std::vector<NoteEvent> withResized(const std::vector<NoteEvent>& notes, int index, bool resizeStart, double newTime, double minLen) {
    if (index < 0 || index >= int(notes.size())) return notes;
    std::vector<NoteEvent> out = notes;
    NoteEvent& n = out[size_t(index)];
    if (resizeStart) n.startSec = std::clamp(newTime, 0.0, n.endSec - minLen);
    else n.endSec = std::max(newTime, n.startSec + minLen);
    return normalizedNotes(std::move(out));
}

std::vector<NoteEvent> withDeleted(const std::vector<NoteEvent>& notes, const std::vector<int>& indices) {
    std::vector<char> drop(notes.size(), 0);
    for (int i : indices) if (i >= 0 && i < int(notes.size())) drop[size_t(i)] = 1;
    std::vector<NoteEvent> out;
    for (size_t i = 0; i < notes.size(); ++i) if (!drop[i]) out.push_back(notes[i]);
    return out;
}

std::vector<NoteEvent> withSelectedDeleted(const std::vector<NoteEvent>& notes) { return withDeleted(notes, selectedIndices(notes)); }

std::vector<NoteEvent> withDuplicated(const std::vector<NoteEvent>& notes) {
    const std::vector<int> picked = selectedIndices(notes);
    if (picked.empty()) return notes;
    double first = notes[size_t(picked[0])].startSec, last = notes[size_t(picked[0])].endSec;
    for (int i : picked) {
        first = std::min(first, notes[size_t(i)].startSec);
        last = std::max(last, notes[size_t(i)].endSec);
    }
    const double offset = last - first;
    std::vector<NoteEvent> out = notes;
    for (NoteEvent& n : out) n.selected = false;
    for (int i : picked) {
        NoteEvent copy = notes[size_t(i)];
        copy.startSec += offset;
        copy.endSec += offset;
        copy.selected = true;
        out.push_back(copy);
    }
    return normalizedNotes(std::move(out));
}

std::vector<NoteEvent> withSelection(const std::vector<NoteEvent>& notes, const std::vector<int>& indices, SelectMode mode) {
    std::vector<NoteEvent> out = notes;
    if (mode == SelectMode::Replace) for (NoteEvent& n : out) n.selected = false;
    for (int i : indices) {
        if (i < 0 || i >= int(out.size())) continue;
        NoteEvent& n = out[size_t(i)];
        n.selected = mode == SelectMode::Toggle ? !n.selected : true;
    }
    return out;
}

std::vector<NoteEvent> withAllSelected(const std::vector<NoteEvent>& notes, bool selected) {
    std::vector<NoteEvent> out = notes;
    for (NoteEvent& n : out) n.selected = selected;
    return out;
}

std::vector<int> selectedIndices(const std::vector<NoteEvent>& notes) {
    std::vector<int> out;
    for (size_t i = 0; i < notes.size(); ++i) if (notes[i].selected) out.push_back(int(i));
    return out;
}

int selectedCount(const std::vector<NoteEvent>& notes) {
    return int(std::count_if(notes.begin(), notes.end(), [](const NoteEvent& n) { return n.selected; }));
}

std::vector<int> indicesInRegion(const std::vector<NoteEvent>& notes, double t0, double t1, int pitchLow, int pitchHigh) {
    if (t1 < t0) std::swap(t0, t1);
    if (pitchHigh < pitchLow) std::swap(pitchLow, pitchHigh);
    std::vector<int> out;
    for (size_t i = 0; i < notes.size(); ++i) {
        const NoteEvent& n = notes[i];
        if (n.pitch >= pitchLow && n.pitch <= pitchHigh && n.startSec <= t1 && n.endSec >= t0) out.push_back(int(i));
    }
    return out;
}

}  // namespace ttr
