#include "analysis/LineTracker.h"

#include <algorithm>
#include <cmath>

namespace ttr {

namespace {
constexpr double kLevelWeight = 0.5;  // semitones of tolerance one e-fold of level change costs
}

void LineTracker::reset() {
    active_.clear();
    nextId_ = 0;
}

void LineTracker::assign(int frame, LinePoint* points, int count) {
    active_.erase(std::remove_if(active_.begin(), active_.end(), [&](const Active& a) { return frame - a.lastFrame > kTrackMaxGapFrames + 1; }), active_.end());
    for (int i = 0; i < count; ++i) {
        LinePoint& p = points[i];
        Active* best = nullptr;
        double bestCost = 1.0e9;
        for (Active& a : active_) {
            const double distance = std::fabs(double(a.midi) - double(p.midi));
            if (a.usedFrame == frame || distance > kTrackToleranceSemis) continue;
            // a weak side peak next to a strong line must not take the line over: level continuity counts too
            const double cost = distance + kLevelWeight * std::fabs(std::log(double(p.level) / std::max(double(a.level), 1.0e-9)));
            if (cost >= bestCost) continue;
            bestCost = cost;
            best = &a;
        }
        if (!best) {
            active_.push_back({nextId_++, frame, 0, p.midi, p.level, frame});
            best = &active_.back();
        }
        best->usedFrame = frame;
        best->lastFrame = frame;
        best->midi = p.midi;
        best->level = p.level;
        best->age = std::min(best->age + 1, 255);
        p.track = best->id;
        p.age = uint8_t(best->age);
    }
}

}  // namespace ttr
