#include "analysis/PitchLines.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {
constexpr float kMinRangeDb = 18.0f, kRangeSpanDb = 50.0f, kGainSpanDb = 40.0f, kLineGamma = 0.85f;
constexpr float kMagnetHarmonicWeight = 0.25f;

bool isVisible(const LinePoint& p) { return p.track >= 0 && !(p.flags & kLineHidden); }
}  // namespace

float lineBrightness(const LinePoint& p, float reference, float gain, float range, bool fundamentals) {
    if (p.level <= 0.0f || reference <= 0.0f) return 0.0f;
    const float rangeDb = kMinRangeDb + std::clamp(range, 0.0f, 1.0f) * kRangeSpanDb;
    const float gainDb = (std::clamp(gain, 0.0f, 1.0f) - 0.5f) * kGainSpanDb;
    const float db = 20.0f * std::log10(p.level / reference) + gainDb;
    const float b = std::pow(std::clamp(1.0f + db / rangeDb, 0.0f, 1.0f), kLineGamma);
    return fundamentals && (p.flags & kLineHarmonic) ? b * kHarmonicDim : b;
}

int rowOfMidi(double midi, double tuningCents) { return int(std::lround(midi - tuningCents / 100.0)); }

double centsFromRow(double midi, double tuningCents) {
    const double shifted = midi - tuningCents / 100.0;
    return (shifted - std::round(shifted)) * 100.0;
}

LineHit PitchLines::hitAt(double sec, double midi, double tolSemis) const {
    LineHit best;
    if (frames <= 0 || hopSec <= 0.0) return best;
    const int centre = int(std::lround(sec / hopSec));
    double bestDistance = tolSemis;
    for (int f = std::max(0, centre - 1); f <= std::min(frames - 1, centre + 1); ++f) {
        const LinePoint* row = frame(f);
        for (int i = 0; i < counts[size_t(f)]; ++i) {
            if (!isVisible(row[i])) continue;
            const double distance = std::fabs(double(row[i].midi) - midi) + (f == centre ? 0.0 : 0.001);
            if (distance > bestDistance) continue;
            bestDistance = distance;
            best = {row[i].track, f, row[i].midi, row[i].level};
        }
    }
    return best;
}

NoteEvent PitchLines::noteForTrack(int track, double tuning) const {
    NoteEvent n;
    if (track < 0 || size_t(track) >= tracks.size()) return n;
    const LineTrack& t = tracks[size_t(track)];
    n.pitch = std::clamp(rowOfMidi(t.meanMidi, tuning), kMinNote, kMaxNote);
    n.startSec = std::max(0.0, t.startSec);
    n.endSec = std::max(t.endSec, n.startSec + kMinLineNoteSec);
    return n;
}

double PitchLines::snapToEdge(double sec, double tolSec) const {
    if (edges.empty()) return sec;
    const auto it = std::lower_bound(edges.begin(), edges.end(), float(sec));
    double best = sec, distance = tolSec;
    for (auto c : {it == edges.begin() ? it : it - 1, it == edges.end() ? it - 1 : it}) {
        if (std::fabs(double(*c) - sec) <= distance) { distance = std::fabs(double(*c) - sec); best = *c; }
    }
    return best;
}

std::vector<LinePoint> PitchLines::trackPoints(int track, std::vector<int>* framesOut) const {
    std::vector<LinePoint> out;
    if (track < 0 || size_t(track) >= tracks.size()) return out;
    const LineTrack& t = tracks[size_t(track)];
    for (int f = std::max(0, t.firstFrame); f <= std::min(frames - 1, t.lastFrame); ++f)
        for (int i = 0; i < counts[size_t(f)]; ++i)
            if (frame(f)[i].track == track) {
                out.push_back(frame(f)[i]);
                if (framesOut) framesOut->push_back(f);
                break;
            }
    return out;
}

int PitchLines::magnetPitch(double t0, double t1, int pitch, double tuning) const {
    if (frames <= 0 || hopSec <= 0.0) return pitch;
    if (t1 < t0) std::swap(t0, t1);
    const int f0 = std::clamp(int(std::floor(t0 / hopSec)), 0, frames - 1);
    const int f1 = std::clamp(std::max(f0, int(std::ceil(t1 / hopSec))), 0, frames - 1);
    double sum[3] = {0.0, 0.0, 0.0};
    for (int f = f0; f <= f1; ++f) {
        const LinePoint* row = frame(f);
        for (int i = 0; i < counts[size_t(f)]; ++i) {
            if (!isVisible(row[i]) || (row[i].flags & kLineDrum)) continue;
            const int offset = rowOfMidi(row[i].midi, tuning) - pitch;
            if (offset >= -1 && offset <= 1) sum[offset + 1] += double(row[i].level) * (row[i].flags & kLineHarmonic ? kMagnetHarmonicWeight : 1.0);
        }
    }
    int best = 1;  // a neighbour must be clearly stronger to win
    for (const int c : {0, 2}) if (sum[c] > sum[best] * 1.000001 + 1e-12) best = c;
    return std::clamp(pitch + best - 1, kMinNote, kMaxNote);
}

}  // namespace ttr
