// Shared plain data types for Tonetrace (no platform dependencies).
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace ttr {

struct PitchLines;  // analysis/PitchLines.h

constexpr int kMinNote = 24;   // C1: lowest row of the note map
constexpr int kMaxNote = 108;  // C8: highest row
constexpr int kNumNotes = kMaxNote - kMinNote + 1;
constexpr double kMaxRecordSeconds = 120.0;
constexpr double kDefaultBpm = 120.0;
constexpr int kPeaksPerSecond = 200;  // waveform overview resolution
constexpr int kExportVelocity = 100;  // drawn notes carry no dynamics

struct NoteEvent {
    int pitch = 60;
    double startSec = 0.0;
    double endSec = 0.0;
    float strength = 1.0f;  // 0..1, only used by Quick Grab / key estimation
    bool selected = false;
    float tonality = 1.0f;  // Quick Grab segment filter: mean harmonic share of the note bins (0..1)
    float sustain = 1.0f;   // Quick Grab segment filter: energy 2nd half / 1st half of the segment (0..2)
};

// Salience matrix: relative amplitude (0..1) per frame and note, after harmonic suppression (Quick Grab only).
struct Salience {
    double hopSec = 0.0;
    int frames = 0;
    double lengthSec = 0.0;
    std::vector<float> values;  // frames * kNumNotes
    float at(int frame, int noteIndex) const { return values[size_t(frame) * kNumNotes + noteIndex]; }
};

// Magnitude spectrogram of a recording: what the note map shows, plus the data for the drum filter.
struct Spectrogram {
    double hopSec = 0.0;
    int frames = 0;
    double lengthSec = 0.0;
    float peak = 0.0f;            // largest value in `raw` (display reference)
    std::vector<float> raw;       // frames * kNumNotes: note energies of the full spectrum
    std::vector<float> harmonic;  // frames * kNumNotes: M_H * raw (harmonic part, "Hide drums")
    std::vector<float> percFrac;  // frames: percussive share (spectrum not explained by H), excess over local median
    std::vector<float> flux;      // frames: positive spectral flux (onset strength), excess over local median
    float rawAt(int f, int n) const { return raw[size_t(f) * kNumNotes + n]; }
    float harmAt(int f, int n) const { return harmonic[size_t(f) * kNumNotes + n]; }
};

// Waveform overview: min/max pair per 1/kPeaksPerSecond.
struct WavePeaks {
    std::vector<float> minMax;  // 2 * count
    int count() const { return int(minMax.size() / 2); }
};

// The recorded audio kept for the preview (mono, decimated, 16 bit).
struct AudioClip {
    double sampleRate = 22050.0;
    std::vector<int16_t> samples;
};

struct FilterSettings {
    bool hideDrums = true;
    float drumFilter = 0.6f;  // 0..1, scales the gate and segment-filter thresholds
};

struct KeyResult {
    int root = -1;  // 0..11 (C..B), -1 = unknown
    bool minor = false;
    float score = 0.0f;
};

// Immutable view of the whole document: drawn notes plus the recording they belong to.
struct Snapshot {
    std::vector<NoteEvent> notes;
    std::shared_ptr<const Spectrogram> spectrogram;  // null before the first recording
    std::shared_ptr<const WavePeaks> peaks;
    std::shared_ptr<const AudioClip> audio;  // null when too long to be stored (preview then plays notes only)
    std::shared_ptr<const PitchLines> lines;  // the thin lines of the note map; null until computed (restored states compute them in the background)
    std::array<float, 12> chroma{};          // pitch-class energy of the recording (key estimate without notes)
    KeyResult key;
    double lengthSec = 0.0;
    double bpm = kDefaultBpm;
    double startPpq = 0.0;
    uint64_t version = 0;
    bool hasRecording() const { return spectrogram != nullptr; }
};

using SnapshotPtr = std::shared_ptr<const Snapshot>;

}  // namespace ttr
