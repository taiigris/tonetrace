// Platform-neutral processor: capture, preview, analysis worker, note model and state.
// Audio-thread methods never allocate or lock. Everything else is for UI/worker/host-control threads.
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "analysis/LiveLines.h"
#include "analysis/LiveSpectrum.h"
#include "analysis/QuickGrab.h"
#include "analysis/TaskControl.h"
#include "analysis/WavePeaks.h"
#include "capture/CaptureBuffer.h"
#include "core/AudioEvents.h"
#include "core/Diagnostics.h"
#include "core/NoteEdit.h"
#include "core/NoteHistory.h"
#include "core/Params.h"
#include "core/Snapshot.h"
#include "core/Types.h"
#include "preview/NoteTable.h"
#include "preview/PreviewSynth.h"
#include "state/UiSettings.h"

namespace ttr {

enum class KernelStatus : int { Idle = 0, Recording = 1, Analysing = 2 };

// What the note map and the waveform lane draw: the live buffers while recording, the snapshot otherwise.
struct MapView {
    SnapshotPtr snapshot;             // keeps the recording alive; may be null
    std::shared_ptr<const void> hold[3];  // keeps the live buffers alive while a frame is drawn
    const float* raw = nullptr;       // frames * kNumNotes
    const float* harmonic = nullptr;  // same layout; may be null (live: lags behind raw by a few frames)
    int harmonicFrames = 0;           // frames of `harmonic` that are finished
    uint64_t generation = 0;          // changes whenever the buffers hold a different recording
    int frames = 0;
    double hopSec = 0.0;
    float peak = 0.0f;                // loudest value so far (display reference)
    const float* wave = nullptr;      // 2 * waveCount (min, max)
    int waveCount = 0;
    double lengthSec = 0.0;
    // Pitch lines: the live buffers while recording, the finished lines otherwise (null / 0 frames when not available)
    const LinePoint* linePoints = nullptr;  // lineFrames * kLineSlots
    const uint8_t* lineCounts = nullptr;
    int lineFrames = 0;
    float linePeak = 0.0f;
    std::shared_ptr<const void> lineHold;
    const uint8_t* fine = nullptr;          // lineFrames * kFineBins: the whole spectrum on the fine pitch grid (null when not available)
    std::shared_ptr<const void> fineHold;   // keeps the live fine buffer alive while a frame is drawn
    const PitchLines* lines = nullptr;      // finished lines (same object as snapshot->lines)
};

// View settings of the thin-line map that live in the editor state (UiSettings), not in the host parameters.
struct LineView {
    bool fundamentals = false;    // dim overtones
    bool alignToSample = true;    // shift the semitone grid by the detected tuning
    float trimCents = 0.0f;       // manual correction, -50..+50
    bool isSpectrum = true;       // View: the full spectrogram (default) instead of the thin lines
    bool grabOnClick = false;     // a click exactly on a strong line grabs it as a note
    bool samplerOnDrag = false;   // a successful Drag MIDI also asks the Live bridge for a sampler track
    GrabEngine engine = GrabEngine::Classic;  // 4.5: Quick Grab engine (Neural/MuScriptor fall back to Classic without a model)
};
constexpr float kMaxTrimCents = 50.0f;

// The editor state (UiSettings) and the kernel's line view carry the same settings.
inline LineView lineViewOf(const UiSettings& s) {
    return {s.fundamentals, s.alignToSample, s.tuningTrim, s.isSpectrum, s.grabOnClick, s.samplerOnDrag, grabEngineFromIndex(s.grabEngine)};
}
inline void storeLineView(const LineView& v, UiSettings* s) {
    s->fundamentals = v.fundamentals;
    s->alignToSample = v.alignToSample;
    s->tuningTrim = v.trimCents;
    s->isSpectrum = v.isSpectrum;
    s->grabOnClick = v.grabOnClick;
    s->samplerOnDrag = v.samplerOnDrag;
    s->grabEngine = static_cast<int>(v.engine);
}

class TonetraceKernel {
public:
    TonetraceKernel();
    ~TonetraceKernel();
    TonetraceKernel(const TonetraceKernel&) = delete;
    TonetraceKernel& operator=(const TonetraceKernel&) = delete;

    // ---- lifecycle (non-real-time) ----
    void prepare(double sampleRate, int maxFrames);  // allocates, starts worker
    void deactivate();                               // stops worker, keeps data readable
    void resetAudioState();                          // audio stopped: clear voices / recording flag state
    // VST3 IAudioProcessor::setProcessing: false ends a running recording (analysis starts) and silences the preview.
    void setProcessing(bool isProcessing);
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
    FilterSettings filterSettings() const;

    // ---- document model (UI thread) ----
    SnapshotPtr snapshot() const;
    MapView mapView() const;
    KernelStatus status() const { return KernelStatus(status_.load()); }
    double recordedSeconds() const;
    double hostBpm() const { return hostBpm_.load(); }
    TimeGrid timeGrid() const;  // recording tempo (host tempo before the first recording) and the Snap parameter
    // Notes: setNotes publishes without touching the history (live drags, selection); editNotes records an undo step.
    void setNotes(std::vector<NoteEvent> notes);
    void editNotes(std::vector<NoteEvent> notes);
    void checkpoint();  // undo step for the current notes (call once before the first setNotes of a gesture)
    bool undo();
    bool redo();
    bool canUndo() const;
    bool canRedo() const;
    // Pitch-line view settings (any thread) and the grid offset they result in: detected tuning (when aligned and
    // reliable) plus the manual trim, in cent.
    LineView lineView() const;
    void setLineView(const LineView& view);
    double tuningOffsetCents() const;
    double detectedTuningCents(bool* isReliable = nullptr) const;
    int quickGrab();  // adds notes from the strongest lines (Quick Grab parameter); returns how many were new
    // Quick Grab on a background thread: returns false when one is already running or there is nothing to analyse.
    // The result is merged as one undo step and announced as a notice.
    bool startQuickGrab();
    void cancelQuickGrab();
    bool quickGrabRunning() const { return grabRunning_.load(); }
    float quickGrabProgress() const { return grabControl_.progress.load(); }
    void resetAll();  // recording, notes and history; undoable (the whole document is one history step)

    // ---- preview ----
    bool previewing() const { return param(kParamPreview) >= 0.5f; }
    double playheadSec() const { return playhead_.load(); }
    void setPreviewStart(double sec) { previewStart_.store(sec); }
    double previewStart() const { return previewStart_.load(); }
    void setPreviewLoop(double startSec, double endSec);  // endSec <= startSec clears the loop
    bool previewLoop(double* startSec, double* endSec) const;
    void audition(int pitch);
    // "Sync to host": while the host transport runs the preview follows the song position (relative to the recording start).
    bool syncToHost() const { return syncToHost_.load(); }
    void setSyncToHost(bool on) { syncToHost_.store(on); }

    // ---- state ----
    std::vector<uint8_t> saveState() const;
    bool loadState(const uint8_t* data, size_t size);  // false + notice/log with the reason on failure

    // ---- diagnostics ----
    Diagnostics& diagnostics() { return diagnostics_; }
    DiagnosticsSnapshot diagnosticsSnapshot() const;  // includes recording length and capture memory
    void notice(const std::string& text, NoticeLevel level = NoticeLevel::Warning) { diagnostics_.pushNotice(text, level); }

private:
    void workerLoop();
    void runAnalysis();
    void startLinesJob();  // pitch lines of a restored recording, on a background thread
    void stopLinesJob();
    void advanceLive();
    void publishLocked(SnapshotPtr snap);  // requires mutateMutex_
    void publishNotesLocked(std::vector<NoteEvent> notes);
    void republishPreviewTable(const Snapshot* snap);
    void restoreCaptureFromSnapshot();
    bool previewActive() const;
    bool renderPreview(int frames);
    void trackBlock(int frames, int numChannels);
    void playBlock(int frames, bool fadeOutOnly);
    void drainAudioEvents();
    bool applyHistoryLocked(bool isUndo);  // requires mutateMutex_
    int mergeGrabbed(const SnapshotPtr& base, std::vector<NoteEvent> grabbed);  // adds notes as one undo step; returns how many
    void reportFallback(const GrabOutcome& outcome, GrabEngine requested);  // Neural/MuScriptor asked for, Classic ran: log + notice
    void mixPreview(float* const* channels, int numChannels, int frames);
    double syncedPosition() const;

    std::array<std::atomic<float>, kNumParams> params_;
    std::atomic<int> status_{0};
    std::atomic<double> playhead_{0.0}, hostBpm_{kDefaultBpm}, hostBeat_{0.0};
    std::atomic<double> previewStart_{0.0}, loopStart_{0.0}, loopEnd_{0.0};
    std::atomic<uint64_t> recordSeq_{0}, analysisSeq_{0};
    std::atomic<bool> workerRun_{false};
    std::atomic<bool> syncToHost_{false}, hostPlaying_{false}, grabRunning_{false};
    std::atomic<double> docBpm_{kDefaultBpm}, docStartPpq_{0.0};
    std::atomic<uint64_t> generation_{0};
    const int instanceId_;
    Diagnostics diagnostics_;
    AudioEventQueue events_;
    std::mutex eventMutex_;  // serialises the consumers of events_ (worker, setProcessing, deactivate)
    TaskControl grabControl_;
    std::thread grabThread_;
    double nsPerTick_ = 1.0;
    int lastBlock_ = 0, maxSeenBlock_ = 0;
    bool isOffline_ = false, wasHostPlaying_ = false;

    double sampleRate_ = 44100.0;
    int maxFrames_ = 0;
    bool wasRecording_ = false, wasPreviewing_ = false, captureFull_ = false, tailPending_ = false, gainsValid_ = false;
    long fadeInDone_ = 0, fadeOutDone_ = 0;
    float sampleGain_ = 0.0f, noteGain_ = 0.0f, dryGain_ = 1.0f;  // smoothed mix gains (audio thread)
    double recBpm_ = kDefaultBpm, recBeat_ = 0.0;
    long playSample_ = 0;
    std::vector<float> mono_, synthBuf_, sampleBuf_;

    CaptureBuffer capture_;
    LiveSpectrum live_;
    PeakBuilder livePeaks_;
    LiveLines liveLines_;
    std::atomic<bool> lineAlign_{true}, lineFundamentals_{false}, lineSpectrum_{true}, lineGrabOnClick_{false}, lineSamplerOnDrag_{false};
    std::atomic<int> lineEngine_{0};
    std::atomic<float> lineTrim_{0.0f};
    std::thread linesThread_;
    TaskControl linesControl_;
    uint64_t liveSeq_ = 0;  // worker thread only
    PreviewSynth synth_;
    NoteTable table_;

    mutable std::mutex liveMutex_;  // live buffers: reallocated by prepare(), read by the UI in mapView() (never taken on the audio thread)
    mutable std::mutex snapMutex_;  // never taken on the audio thread
    SnapshotPtr snapshot_;
    NoteHistory history_;           // guarded by mutateMutex_
    mutable std::mutex mutateMutex_;  // serialises read-modify-publish of the snapshot (UI + worker)
    std::thread worker_;
};

}  // namespace ttr
