// Time / pitch window shared by the waveform lane, the note map, the piano keys and the scroll bar.
#pragma once
#include "core/Params.h"
#include "core/Types.h"

struct TTRViewport {
    double scrollSec = 0.0;
    double pxPerSec = ttr::kBasePixelsPerSecond;
    double scrollY = 0.0;      // points from the top of the highest row
    double rowH = 15.0;        // height of one semitone row
    double contentSec = 1.0;   // length of the scrollable timeline
    bool hasCursor = false;    // crosshair is shown
    double cursorSec = 0.0;
    double cursorPitch = 60.0;  // fractional MIDI pitch under the pointer
    // Draw statistics of the note map (diagnostics panel)
    double frameMsAvg = 0.0;    // exponential average of the time to draw one frame
    unsigned long drawCount = 0;

    double timeForX(double x) const { return scrollSec + x / pxPerSec; }
    double xForTime(double t) const { return (t - scrollSec) * pxPerSec; }
    double yTopForPitch(int pitch) const { return (ttr::kMaxNote - pitch) * rowH - scrollY; }
    double pitchForY(double y) const { return ttr::kMaxNote - (y + scrollY) / rowH + 0.5; }  // fractional, row centre = integer
    double contentHeight() const { return ttr::kNumNotes * rowH; }
};
