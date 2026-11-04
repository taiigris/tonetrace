// Platform-neutral processor of Tonetrace 5 (NeuralNote-style): load a song or record one, transcribe every instrument
// with MuScriptor (notes stream in chunk by chunk), play the original and the notes, export one MIDI track per instrument.
// Audio-thread methods never allocate or lock. Everything else is for UI/worker/host-control threads.
#pragma once
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "analysis/TaskControl.h"
#include "analysis/WavePeaks.h"
#include "capture/CaptureBuffer.h"
#include "core/AudioEvents.h"
#include "core/Diagnostics.h"
#include "core/Mixer.h"
#include "core/Params.h"
#include "core/Types.h"
#include "preview/NoteSynth.h"
#include "preview/PlayTable.h"

namespace ttr {

// The waveform of a running recording (min/max pairs, mono), kept alive while the UI draws it.
struct RecordingView {
    const float* minMax = nullptr;
    int count = 0;  // pairs
    std::shared_ptr<const void> hold;
};

class TonetraceKernel {
public:
    TonetraceKernel();
    ~TonetraceKernel();
    TonetraceKernel(const TonetraceKernel&) = delete;
    TonetraceKernel& operator=(const TonetraceKernel&) = delete;

    // ---- lifecycle (non-real-time) ----
    void prepare(double sampleRate, int maxFrames);  // allocates, starts the worker
    void deactivate();                               // stops the worker and any running job; data stays readable
    void setProcessing(bool isProcessing);           // false ends a running recording (it gets transcribed)
    void setOffline(bool isOffline);
    void setHostName(const std::string& name);
    void setChannels(int channels);
    int instanceId() const { return instanceId_; }

    // ---- audio thread ----
    void process(float* const* channels, int numChannels, int frames);
    void setHostTransport(double bpm, double beat, bool isPlaying = false);

    // ---- parameters (any thread) ----
    float param(int id) const;
    void setParam(int id, float value);  // clamps; atomic store only, safe on any thread
    ParamValues allParams() const;

    // ---- document ----
    DocumentPtr document() const;
    // Loads a song file in the background: the audio is playable as soon as it is decoded, the notes follow chunk by
    // chunk. Replaces the current document (a running job is cancelled). False for unsupported files or while recording.
    bool loadSong(const std::string& path);
    void cancelTranscription();  // returns at once; the document keeps the notes found so far
    bool isWorking() const { return jobRunning_.load(); }
    void clearDocument();        // nothing loaded (the drop zone shows again)
    bool isRecording() const { return wasRecordingFlag_.load(); }
    double recordedSeconds() const;
    RecordingView recordingView() const;

    // ---- transport (UI thread) ----
    void play();
    void pause();
    void togglePlay();
    bool isPlaying() const { return playing_.load(); }
    void seek(double sec);
    double positionSec() const { return positionSec_.load(); }

    // ---- mixer ----
    MixerState mixer() const;
    void setInstrumentMix(int program, const InstrumentMix& mix);
    float instrumentLevel(int program) const;  // peak of the last block, 0..1+
    float masterLevel() const { return masterLevel_.load(std::memory_order_relaxed); }

    // ---- MIDI export ----
    double hostBpm() const { return hostBpm_.load(); }
    double exportBpm() const;           // the user's value, or the host tempo until one is set
    void setExportBpm(double bpm);      // clamped to 20..400
    // The notes of the audible instruments (all when `allInstruments`), one track per instrument.
    std::vector<uint8_t> buildMidi(bool allInstruments = false) const;
    std::string midiFileName() const;

    // ---- state ----
    std::vector<uint8_t> saveState() const;
    bool loadState(const uint8_t* data, size_t size);  // false + notice/log with the reason on failure

    // ---- diagnostics ----
    Diagnostics& diagnostics() { return diagnostics_; }
    DiagnosticsSnapshot diagnosticsSnapshot() const;
    void notice(const std::string& text, NoticeLevel level = NoticeLevel::Warning) { diagnostics_.pushNotice(text, level); }

private:
    // documents (KernelJobs.cpp)
    void publish(DocumentPtr doc);
    bool updateDocument(uint64_t id, const std::function<void(Document&)>& change);  // false when another document is current
    void republishPlayTable();
    void setPlayAudio(std::shared_ptr<const SongAudio> audio);
    void collectRetiredAudio(bool force);
    void startJob(std::function<void(TaskControl&)> job);
    void stopJob();
    void runLoadSong(TaskControl& control, uint64_t id, const std::string& path);
    void runTranscription(TaskControl& control, uint64_t id, std::vector<float> audio16k);
    bool waitForModel(TaskControl& control, uint64_t id, std::string* reason);
    void runReloadAudio(TaskControl& control, uint64_t id, const std::string& path);
    uint64_t nextDocumentId() { return ++documentIds_; }
    // recording (KernelWorker.cpp)
    void workerLoop();
    void drainAudioEvents();
    void finishRecording();
    // audio (KernelAudio.cpp)
    void recordBlock(float* const* channels, int numChannels, int frames);
    void playBlock(float* const* channels, int numChannels, int frames);
    void scheduleNotes(const PlayTable::View& table, double fromSec, int frames, bool startSounding);
    void updateMeters(float* const* channels, int numChannels, int frames);

    std::array<std::atomic<float>, kNumParams> params_;
    const int instanceId_;
    Diagnostics diagnostics_;
    AudioEventQueue events_;
    std::mutex eventMutex_;
    std::atomic<double> hostBpm_{kDefaultBpm}, exportBpm_{0.0};
    std::atomic<bool> isOffline_{false};

    // document
    mutable std::mutex docMutex_;
    DocumentPtr doc_;
    MixerState mixer_;  // guarded by docMutex_
    std::atomic<uint64_t> documentIds_{0};
    std::atomic<double> docLength_{0.0};

    // background job (load, transcription)
    std::thread jobThread_;
    TaskControl jobControl_;
    std::atomic<bool> jobRunning_{false};
    std::atomic<uint64_t> jobGeneration_{0};  // a job only runs (and clears jobRunning_) while it is the latest
    std::mutex jobMutex_;

    // song audio handed to the audio thread: raw pointer + owners retired only after the audio thread moved on
    std::atomic<const SongAudio*> playAudio_{nullptr};
    std::shared_ptr<const SongAudio> playAudioOwner_;
    struct Retired { std::shared_ptr<const SongAudio> audio; uint64_t epoch; };
    std::vector<Retired> retired_;
    std::mutex audioMutex_;
    std::atomic<uint64_t> audioEpoch_{0};

    // transport + playback (audio thread state below the atomics)
    std::atomic<bool> playing_{false};
    std::atomic<double> positionSec_{0.0}, seekRequest_{-1.0};
    PlayTable table_;
    NoteSynth synth_;
    bool wasPlaying_ = false;
    std::vector<float> synthBuf_;
    double sampleRate_ = 44100.0;
    int maxFrames_ = 0;
    std::array<std::atomic<float>, NoteSynth::kLevelSlots> levels_{};
    std::atomic<float> masterLevel_{0.0f};

    // recording
    CaptureBuffer captureL_, captureR_;
    PeakBuilder recordPeaks_;
    mutable std::mutex recordMutex_;  // recordPeaks_ buffer (re)allocation vs. the UI reading it
    std::atomic<bool> wasRecordingFlag_{false};
    std::atomic<uint64_t> recordStops_{0};
    bool wasRecording_ = false;
    bool firstBlock_ = true;
    double nsPerTick_ = 1.0;

    std::thread worker_;
    std::atomic<bool> workerRun_{false};
};

}  // namespace ttr
