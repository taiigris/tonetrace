// The shared view window of the waveform lane and the piano roll: time zoom and scroll, row height and vertical scroll.
#pragma once
#include <algorithm>
#include <cmath>

struct TTRTimeline {
    double pxPerSec = 50.0;   // horizontal zoom
    double scrollSec = 0.0;   // time at the left edge of the note area
    double contentSec = 0.0;  // length of the song
    double rowH = 12.0;       // one semitone
    double scrollY = 0.0;     // pixels from the top row (C8) to the top of the visible area
    bool isFit = true;        // the whole song fills the width (until the user zooms)
    bool follow = true;       // the view keeps the playhead visible while playing

    double xForTime(double sec) const { return (sec - scrollSec) * pxPerSec; }
    double timeForX(double x) const { return scrollSec + x / pxPerSec; }
    double visibleSec(double width) const { return width / pxPerSec; }
    void clampScroll(double width) {
        const double maxScroll = std::max(0.0, contentSec - visibleSec(width) + 0.5);
        scrollSec = std::clamp(std::isfinite(scrollSec) ? scrollSec : 0.0, 0.0, maxScroll);
    }
    void fit(double width) {
        if (contentSec <= 0.0 || width <= 0.0) return;
        pxPerSec = width / contentSec;
        scrollSec = 0.0;
    }
};
