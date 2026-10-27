// Drum hits that sit on the beat grid (kick of a four-on-the-floor pattern): what pulls a held tone down for an instant.
#pragma once
#include <vector>

#include "core/Types.h"

namespace ttr {

constexpr double kBeatToleranceSec = 0.06;
constexpr double kBeatPulseRatio = 1.3;  // level after the beat / level before the next beat
constexpr double kBeatReExcite = 1.3;    // level after a beat / level before the previous one: a pulse comes back, a decay does not

struct BeatGrid {
    double beatSec = 0.0;       // beat period; 0 = no usable grid
    double firstBeatSec = 0.0;  // time of the first beat after the start of the recording
    bool isValid() const { return beatSec > 0.0; }
};

// The grid from the host tempo and the beat position of the recording start, checked against the percussive onsets of the
// recording: when the onsets line up clearly better on a grid of their own (0.4-0.75 s period) that one is used, so a wrong or
// missing host tempo does not misplace the kicks. No usable tempo and no clear pulse give an invalid grid.
BeatGrid findBeatGrid(const Spectrogram& spec, double hostBpm, double hostStartPpq, float drumFilter);

// 1 for the frames of a percussive onset within kBeatToleranceSec of a beat.
std::vector<char> beatHitFrames(const Spectrogram& spec, const BeatGrid& grid, float drumFilter);

// 1 for every frame within kBeatToleranceSec of a beat (hit or not): a pump dip follows the kick, which a flagged onset does not always show.
std::vector<char> beatFrames(const Spectrogram& spec, const BeatGrid& grid);

// True when the level of a pitch over the frames [first, last] pulses with the beat the way a kick does: strongest right after
// the beat and decayed shortly before the next one (a pumped bass is the opposite: low at the beat, back up before the next).
// Needs at least minBeats whole beats inside the run; an invalid grid gives false.
bool isBeatPulse(const std::vector<float>& support, int first, int last, double hopSec, const BeatGrid& grid, int minBeats);

}  // namespace ttr
