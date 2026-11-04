// Shared plain data types for Tonetrace 5 (no platform dependencies): notes, the song audio and the document.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ttr {

constexpr int kMinNote = 21;   // A0: lowest row of the piano roll
constexpr int kMaxNote = 108;  // C8: highest row
constexpr int kNumNotes = kMaxNote - kMinNote + 1;
constexpr double kMaxRecordSeconds = 120.0;
constexpr double kDefaultBpm = 120.0;
constexpr int kPeaksPerSecond = 100;  // waveform overview resolution
constexpr int kExportVelocity = 100;  // the model predicts no dynamics
constexpr int kNoProgram = -1;        // a note without an instrument
constexpr int kDrumProgram = 128;     // drum hit: pitch is the GM percussion note
constexpr int kMaxProgram = 128;

struct NoteEvent {
    int pitch = 60;
    double startSec = 0.0;
    double endSec = 0.0;
    int program = kNoProgram;  // GM program 0..127, kDrumProgram for drums
};

// Waveform overview: min/max pair per 1/kPeaksPerSecond (mono).
struct WavePeaks {
    std::vector<float> minMax;  // 2 * count
    int count() const { return int(minMax.size() / 2); }
};

// The audio the player plays: 16 bit, interleaved, any rate (a loaded song is 44.1 kHz stereo).
struct SongAudio {
    double sampleRate = 44100.0;
    int channels = 2;
    std::vector<int16_t> samples;
    size_t frames() const { return channels > 0 ? samples.size() / size_t(channels) : 0; }
    double seconds() const { return sampleRate > 0.0 ? double(frames()) / sampleRate : 0.0; }
};

enum class TranscriptionPhase : int {
    None = 0,      // nothing loaded, or a restored document whose notes are already complete
    Loading = 1,   // decoding the file
    Running = 2,   // the model works; notes arrive chunk by chunk
    Done = 3,
    Failed = 4,    // message says why (no model, download running, damaged file, ...)
    Cancelled = 5,
};

struct Transcription {
    TranscriptionPhase phase = TranscriptionPhase::None;
    float progress = 0.0f;       // 0..1
    double finalizedSec = 0.0;   // every note ending before this is known
    std::string message;         // reason of a failure
    bool isActive() const { return phase == TranscriptionPhase::Loading || phase == TranscriptionPhase::Running; }
};

// Immutable view of the whole document. Replaced (never changed) on every update.
struct Document {
    std::string sourceName;  // "My Song.mp3" or "Recording"
    std::string sourcePath;  // the file (empty for a recording); a restored song reloads its audio from here
    bool isRecording = false;
    std::shared_ptr<const SongAudio> audio;  // null while loading or when the file is gone
    std::shared_ptr<const WavePeaks> peaks;
    std::vector<NoteEvent> notes;  // sorted by start
    double lengthSec = 0.0;
    Transcription transcription;
    uint64_t id = 0;       // one per loaded song or recording; background jobs only update their own document
    uint64_t version = 0;  // changes with every update
};

using DocumentPtr = std::shared_ptr<const Document>;

}  // namespace ttr
