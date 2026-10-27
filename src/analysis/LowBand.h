// Bass register with long analysis windows: semitones of a 44 Hz bass are only 2.6 Hz apart, so the note analysis of the
// map (186 ms window) cannot separate them. Quick Grab uses these energies for the notes below kLowTopNote instead.
#pragma once
#include <vector>

namespace ttr {

constexpr int kLowFirstNote = 24;   // C1 (= kMinNote)
constexpr int kLowNotes = 24;       // C1..B2
constexpr int kLowTopNote = kLowFirstNote + kLowNotes;
constexpr int kLowLongWindow = 16384;   // 0.74 s at 22.05 kHz: notes up to B1 (F1 = 43.7 Hz, semitone 2.6 Hz)
constexpr int kLowShortWindow = 8192;   // 0.37 s: C2..B2
constexpr int kLowLongTopNote = 36;     // notes below use the long window

// Amplitude-like energies, frames * kLowNotes, in the units of Spectrogram::raw (Hann magnitude of a 4096 window).
// Frame f is centred at sample f * hop of the decimated signal; every second frame is computed, the rest interpolated.
std::vector<float> computeLowEnergies(const float* samples, long count, double rate, int frames, int hop);

}  // namespace ttr
