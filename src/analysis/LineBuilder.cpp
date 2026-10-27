#include "analysis/LineBuilder.h"

#include <algorithm>
#include <cmath>

#include "analysis/DrumFilter.h"
#include "analysis/LineFrames.h"
#include "analysis/LineOnsets.h"
#include "analysis/LineTracker.h"
#include "analysis/SpectralFrames.h"

namespace ttr {

namespace {

constexpr int kProgressStride = 128;
constexpr int kSplitMinPoints = 8, kSplitMargin = 4, kBeforeFrom = 8, kBeforeTo = 5, kAfterFrom = 4, kAfterTo = 8, kCutFrom = 3, kCutTo = 2;
constexpr float kSplitRise = 1.4f, kSplitDip = 0.85f;
constexpr int kEdgeFrames = 24;  // longer than the tail a 16384-sample window smears (16 frames)
constexpr float kEdgeLevel = 0.5f;  // a Hann window centred on a note edge sees half of the note
constexpr float kHarmonicShare = 0.5f;
constexpr int kTuningMinPoints = 8;
constexpr float kDrumFilterStrength = 0.6f;
constexpr double kBlipSec = 0.12;  // shorter tracks the segment filter doubts are clicks, not notes

struct Ref { int frame, slot; };
using TrackRefs = std::vector<std::vector<Ref>>;

LinePoint& pointOf(PitchLines& L, const Ref& r) { return L.points[size_t(r.frame) * kLineSlots + size_t(r.slot)]; }

TrackRefs collectRefs(PitchLines& L, int trackCount) {
    TrackRefs refs(size_t(std::max(0, trackCount)));
    for (int f = 0; f < L.frames; ++f)
        for (int s = 0; s < L.counts[size_t(f)]; ++s) {
            const int id = L.points[size_t(f) * kLineSlots + size_t(s)].track;
            if (id >= 0 && size_t(id) < refs.size()) refs[size_t(id)].push_back({f, s});
        }
    return refs;
}

float maxLevel(PitchLines& L, const std::vector<Ref>& r, int fromFrame, int toFrame) {
    float best = 0.0f;
    for (const Ref& x : r) if (x.frame >= fromFrame && x.frame <= toFrame) best = std::max(best, pointOf(L, x).level);
    return best;
}

// A note that starts again on the same pitch (repeated pluck) keeps one track while the gap is bridged: cut it at the onsets.
void splitAtOnsets(PitchLines& L, TrackRefs& refs) {
    for (size_t t = 0; t < refs.size(); ++t) {
        if (refs[t].size() < size_t(kSplitMinPoints)) continue;
        for (const float onset : L.onsets) {
            const int fo = int(std::lround(onset / L.hopSec));
            const std::vector<Ref>& r = refs[t];
            if (fo <= r.front().frame + kSplitMargin || fo >= r.back().frame - kSplitMargin) continue;
            const float before = maxLevel(L, r, fo - kBeforeFrom, fo - kBeforeTo), after = maxLevel(L, r, fo + kAfterFrom, fo + kAfterTo);
            if (before <= 0.0f || after <= 0.0f) continue;
            int cut = fo;
            float lowest = 1.0e9f;
            for (const Ref& x : r)
                if (x.frame >= fo - kCutFrom && x.frame <= fo + kCutTo && pointOf(L, x).level < lowest) { lowest = pointOf(L, x).level; cut = x.frame; }
            const bool rises = after >= kSplitRise * before, dips = lowest < kSplitDip * std::min(before, after);
            if (!rises && !dips) continue;
            std::vector<Ref> later;
            for (const Ref& x : r) if (x.frame >= cut) later.push_back(x);
            refs[t].erase(std::remove_if(refs[t].begin(), refs[t].end(), [&](const Ref& x) { return x.frame >= cut; }), refs[t].end());
            for (const Ref& x : later) pointOf(L, x).track = int32_t(refs.size());
            refs.push_back(std::move(later));
            break;  // the new track is examined later in this loop
        }
    }
}

// Edge times from the half-level crossing of the first / last frames (levels held across gaps).
void trackEdges(PitchLines& L, const std::vector<Ref>& r, double* start, double* end) {
    const int first = r.front().frame, last = r.back().frame, n = last - first + 1;
    std::vector<float> lv(size_t(n), 0.0f);
    for (const Ref& x : r) lv[size_t(x.frame - first)] = pointOf(L, x).level;
    for (int i = 1; i < n; ++i) if (lv[size_t(i)] <= 0.0f) lv[size_t(i)] = lv[size_t(i - 1)];
    const int head = std::min(n, kEdgeFrames);
    const float startRef = kEdgeLevel * *std::max_element(lv.begin(), lv.begin() + head);
    const float endRef = kEdgeLevel * *std::max_element(lv.end() - head, lv.end());
    *start = first * L.hopSec;
    for (int i = 1; i < n; ++i)
        if (lv[size_t(i)] >= startRef) {
            if (lv[size_t(i - 1)] < startRef) *start = (first + i - 1 + (startRef - lv[size_t(i - 1)]) / (lv[size_t(i)] - lv[size_t(i - 1)])) * L.hopSec;
            break;
        }
    *end = last * L.hopSec;
    for (int i = n - 2; i >= 0; --i)
        if (lv[size_t(i)] >= endRef) {
            if (lv[size_t(i + 1)] < endRef) *end = (first + i + (lv[size_t(i)] - endRef) / (lv[size_t(i)] - lv[size_t(i + 1)])) * L.hopSec;
            break;
        }
}

void describeTracks(PitchLines& L, const TrackRefs& refs, const Spectrogram* spec) {
    L.tracks.assign(refs.size(), LineTrack{});
    for (size_t t = 0; t < refs.size(); ++t) {
        const std::vector<Ref>& r = refs[t];
        LineTrack& tr = L.tracks[t];
        if (r.empty()) { tr.flags = kLineHidden; continue; }
        double weight = 0.0, sum = 0.0;
        int harmonic = 0;
        for (const Ref& x : r) {
            const LinePoint& p = pointOf(L, x);
            sum += double(p.midi) * p.level;
            weight += p.level;
            tr.peakLevel = std::max(tr.peakLevel, p.level);
            harmonic += (p.flags & kLineHarmonic) ? 1 : 0;
        }
        tr.firstFrame = r.front().frame;
        tr.lastFrame = r.back().frame;
        tr.points = int(r.size());
        tr.meanMidi = float(weight > 0.0 ? sum / weight : 0.0);
        if (tr.points < kMinTrackPoints || tr.meanMidi < kMinNote - 0.5f || tr.meanMidi > kMaxNote + 0.5f) tr.flags |= kLineHidden;  // too short, or beyond the rows of the map
        else if (float(harmonic) >= kHarmonicShare * float(tr.points)) tr.flags |= kLineHarmonic;
        trackEdges(L, r, &tr.startSec, &tr.endSec);
        const float* nearest = std::lower_bound(L.onsets.data(), L.onsets.data() + L.onsets.size(), float(tr.startSec - kOnsetSnapSec));
        if (nearest != L.onsets.data() + L.onsets.size() && std::fabs(double(*nearest) - tr.startSec) <= kOnsetSnapSec && *nearest < tr.endSec) tr.startSec = *nearest;
        tr.endSec = std::max(tr.endSec, tr.startSec + L.hopSec);
        if (spec && spec->frames == L.frames && !(tr.flags & kLineHidden)) {
            NoteSegment seg;
            seg.note.pitch = std::clamp(int(std::lround(tr.meanMidi)), kMinNote, kMaxNote);
            seg.note.startSec = tr.startSec;
            seg.note.endSec = tr.endSec;
            seg.noteIndex = seg.note.pitch - kMinNote;
            seg.first = std::clamp(int(std::lround(tr.startSec / L.hopSec)), tr.firstFrame, tr.lastFrame);  // the note itself, not the window's smear
            seg.last = std::clamp(int(std::lround(tr.endSec / L.hopSec)), seg.first, tr.lastFrame);
            annotateSegment(*spec, &seg);
            // plucks and short notes also look like "short decay" to the segment filter; for lines only what is not tonal at all
            // (noise bursts) or a falling sweep (kick) counts as drum
            const DrumReason reason = drumReason(*spec, seg, kDrumFilterStrength);
            const bool isBlip = tr.endSec - tr.startSec < kBlipSec;
            if (reason == DrumReason::LowTonality || reason == DrumReason::DownwardGlide || (reason != DrumReason::None && isBlip)) tr.flags |= kLineDrum;
        }
        for (const Ref& x : r) pointOf(L, x).flags = tr.flags;
    }
}

// Circular mean of the deviation from the semitone grid (period 100 cent) of the loud, long, non-harmonic lines.
void estimateTuning(PitchLines& L) {
    double c = 0.0, s = 0.0, total = 0.0;
    for (int f = 0; f < L.frames; ++f)
        for (int i = 0; i < L.counts[size_t(f)]; ++i) {
            const LinePoint& p = L.points[size_t(f) * kLineSlots + size_t(i)];
            if (p.track < 0 || (p.flags & (kLineHidden | kLineHarmonic | kLineDrum)) || L.tracks[size_t(p.track)].points < kTuningMinPoints) continue;
            const double angle = 2.0 * M_PI * (double(p.midi) - std::round(double(p.midi))), w = double(p.level) * p.level;
            c += w * std::cos(angle);
            s += w * std::sin(angle);
            total += w;
        }
    L.tuningCents = 0.0f;
    L.tuningConfidence = 0.0f;
    if (total <= 0.0) return;
    L.tuningCents = float(std::atan2(s, c) / (2.0 * M_PI) * 100.0);
    L.tuningConfidence = float(std::hypot(c, s) / total);
}

}  // namespace

void finalizeLines(PitchLines* lines, std::vector<float> onsets, const Spectrogram* spectrogram) {
    PitchLines& L = *lines;
    L.onsets = std::move(onsets);
    int trackCount = 0;
    for (const LinePoint& p : L.points) trackCount = std::max(trackCount, int(p.track) + 1);
    TrackRefs refs = collectRefs(L, trackCount);
    splitAtOnsets(L, refs);
    describeTracks(L, refs, spectrogram);
    L.peak = 0.0f;
    float anyPeak = 0.0f;
    for (const LinePoint& p : L.points) {
        if (p.track < 0 || (p.flags & kLineHidden)) continue;
        anyPeak = std::max(anyPeak, p.level);
        if (!(p.flags & kLineDrum)) L.peak = std::max(L.peak, p.level);
    }
    if (L.peak <= 0.0f) L.peak = anyPeak;
    estimateTuning(L);
    L.edges = L.onsets;
    for (const LineTrack& t : L.tracks)
        if (!(t.flags & kLineHidden)) { L.edges.push_back(float(t.startSec)); L.edges.push_back(float(t.endSec)); }
    std::sort(L.edges.begin(), L.edges.end());
}

std::shared_ptr<PitchLines> computePitchLines(const float* samples, long count, double rate, const Spectrogram* spectrogram, TaskControl* control) {
    if (!samples || count < kHopSize || rate <= 0.0) return nullptr;
    auto lines = std::make_shared<PitchLines>();
    lines->frames = int(count / kHopSize) + 1;
    lines->hopSec = double(kHopSize) / rate;
    lines->points.assign(size_t(lines->frames) * kLineSlots, LinePoint{});
    lines->counts.assign(size_t(lines->frames), 0);
    lines->fine.assign(size_t(lines->frames) * kFineBins, 0);
    LineFrameAnalyzer analyzer(rate);
    LineTracker tracker;
    bool any = false;
    for (int f = 0; f < lines->frames; ++f) {
        if (control && f % kProgressStride == 0 && control->cancelled()) return nullptr;
        LinePoint* row = &lines->points[size_t(f) * kLineSlots];
        const int n = analyzer.compute(samples, count, f, row);
        tracker.assign(f, row, n);
        lines->counts[size_t(f)] = uint8_t(n);
        analyzer.fineRow(&lines->fine[size_t(f) * kFineBins]);
        any = any || n > 0;
    }
    if (!any) return nullptr;
    finalizeLines(lines.get(), detectOnsets(samples, count, rate), spectrogram);
    return lines;
}

}  // namespace ttr
