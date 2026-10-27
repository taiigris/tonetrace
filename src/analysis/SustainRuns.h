// Segmentation of one pitch's salience column into held notes (SustainGrab): hysteresis, valley splitting, bridging, edge trim.
#pragma once
#include <vector>

namespace ttr {

struct Run { int first, last; };  // inclusive frame indices

constexpr float kValleyRatio = 0.1f;    // a dip of more than 20 dB between two peaks ends a note (pump dips stay below 12 dB)
constexpr float kAttackValleyRatio = 0.5f;  // a valley of 6 dB or more ends a note when a new attack follows it (a pump release rises slowly)
constexpr float kAttackRise = 1.6f;         // new attack: +4 dB within two frames (a pump release rises by less than 3 dB)
constexpr float kSilenceRatio = 0.12f;       // a gap that drops below -30 dB is silence: no drum hit masks a tone that far (it adds energy)
constexpr int kMaskLeadFrames = 2;          // the hit may sit this many frames around the start of the gap
constexpr float kHeldThroughRatio = 0.5f;   // a gap in which the raw level stays within 6 dB of the flanks is a masked tone (drums) ...
constexpr double kHeldThroughMaxSec = 1.0;  // ... for at most this long
constexpr float kEdgeTrimRatio = 0.35f; // note edges: first / last frame at this share of the note's own peak

// A run starts at a frame >= on and holds while the value stays >= off.
std::vector<Run> hysteresisRuns(const std::vector<float>& v, float on, float off);
// A valley / gap that starts on a drum hit on the beat (beatHits) and lasts at most longBridge frames is a masked tone, not an ending.
// Splits runs at valleys deeper than kValleyRatio of the lower flanking peak, or deeper than kAttackValleyRatio when the
// support rises like a new attack right after it (recursively, deepest first).
std::vector<Run> valleyRuns(const std::vector<float>& v, const std::vector<float>& attackSupport, const std::vector<float>& rawSupport, const std::vector<char>& beatHits, int longBridge, const std::vector<Run>& runs);
// Joins two runs when the gap is at most shortBridge frames, or at most longBridge frames and no deeper than a pump dip.
// The depth is judged on `support` (the energy of the pitch before the harmonic cancellation, which a drum hit removes
// from the picks without the tone having stopped); runs and edges are on the picks `v`.
std::vector<Run> bridgeRuns(const std::vector<float>& support, const std::vector<float>& attackSupport, const std::vector<float>& maskSupport, const std::vector<char>& beatHits, const std::vector<Run>& runs, int shortBridge, int longBridge, int heldThroughMax);
// Moves the edges inwards to the first / last frame of at least `ratio` of the run's peak.
Run trimRun(const std::vector<float>& v, Run run, float ratio);

}  // namespace ttr
