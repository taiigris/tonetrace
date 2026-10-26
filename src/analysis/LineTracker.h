// Links the peaks of consecutive frames into lines (causal, so the live view can use it too).
#pragma once
#include <vector>

#include "analysis/PitchLines.h"

namespace ttr {

class LineTracker {
public:
    void reset();
    // Gives every point of frame `frame` a track id and age (new id when no active line is within kTrackToleranceSemis).
    void assign(int frame, LinePoint* points, int count);
    int trackCount() const { return nextId_; }

private:
    struct Active { int id, lastFrame, age; float midi, level; int usedFrame; };
    std::vector<Active> active_;
    int nextId_ = 0;
};

}  // namespace ttr
