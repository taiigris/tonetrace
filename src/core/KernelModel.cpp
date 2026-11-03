// TonetraceKernel: snapshot publication, note editing with undo, Quick Grab and state persistence (non-audio threads).
#include <algorithm>
#include <cmath>

#include "analysis/PitchLines.h"
#include "analysis/QuickGrab.h"
#include "core/Logger.h"
#include "core/TonetraceKernel.h"
#include "state/StateCodec.h"

namespace ttr {

namespace {
constexpr float kPreviewVelocity = float(kExportVelocity) / 127.0f;
}

SnapshotPtr TonetraceKernel::snapshot() const {
    std::lock_guard<std::mutex> lock(snapMutex_);
    return snapshot_;
}

MapView TonetraceKernel::mapView() const {
    MapView v;
    v.snapshot = snapshot();
    v.generation = generation_.load();
    if (status() != KernelStatus::Idle) {
        std::lock_guard<std::mutex> liveLock(liveMutex_);
        v.hold[0] = live_.rawHold();
        v.hold[1] = live_.harmonicHold();
        v.hold[2] = livePeaks_.hold();
        v.raw = live_.raw();
        v.harmonic = live_.harmonic();
        v.harmonicFrames = live_.harmonicFrames();
        v.frames = live_.frames();
        v.hopSec = live_.hopSec();
        v.peak = live_.peak();
        v.wave = livePeaks_.data();
        v.waveCount = livePeaks_.count();
        v.linePoints = liveLines_.points();
        v.lineCounts = liveLines_.counts();
        v.lineFrames = liveLines_.frames();
        v.linePeak = liveLines_.peak();
        v.lineHold = liveLines_.hold();
        v.fine = liveLines_.fine();
        v.fineHold = liveLines_.holdFine();
        v.lengthSec = recordedSeconds();
        return v;
    }
    if (!v.snapshot) return v;
    if (const Spectrogram* spec = v.snapshot->spectrogram.get()) {
        v.raw = spec->raw.data();
        v.harmonic = spec->harmonic.empty() ? nullptr : spec->harmonic.data();
        v.harmonicFrames = v.harmonic ? spec->frames : 0;
        v.frames = spec->frames;
        v.hopSec = spec->hopSec;
        v.peak = spec->peak;
    }
    if (v.snapshot->peaks) {
        v.wave = v.snapshot->peaks->minMax.data();
        v.waveCount = v.snapshot->peaks->count();
    }
    if (const PitchLines* lines = v.snapshot->lines.get()) {
        v.lines = lines;
        v.linePoints = lines->points.data();
        v.lineCounts = lines->counts.data();
        v.lineFrames = lines->frames;
        v.linePeak = lines->peak;
        v.lineHold = v.snapshot->lines;
        v.fine = lines->fine.empty() ? nullptr : lines->fine.data();
    }
    v.lengthSec = v.snapshot->lengthSec;
    return v;
}

TimeGrid TonetraceKernel::timeGrid() const {
    TimeGrid grid;
    grid.snapMode = int(param(kParamSnap));
    const SnapshotPtr snap = snapshot();
    grid.bpm = snap && snap->hasRecording() ? snap->bpm : hostBpm();
    grid.startPpq = snap && snap->hasRecording() ? snap->startPpq : 0.0;
    if (snap) grid.lines = snap->lines;
    return grid;
}

void TonetraceKernel::republishPreviewTable(const Snapshot* snap) {
    std::vector<PreviewNote> notes;
    double loop = 0.0;
    if (snap) {
        loop = snap->lengthSec;
        for (const NoteEvent& n : snap->notes) {
            notes.push_back({float(n.startSec), float(n.endSec), kPreviewVelocity, n.pitch});
            loop = std::max(loop, n.endSec);
        }
    }
    table_.publish(notes.data(), int(notes.size()), loop);
}

void TonetraceKernel::publishLocked(SnapshotPtr snap) {
    {
        std::lock_guard<std::mutex> lock(snapMutex_);
        snapshot_ = snap;
    }
    docBpm_.store(snap ? snap->bpm : hostBpm(), std::memory_order_relaxed);
    docStartPpq_.store(snap ? snap->startPpq : 0.0, std::memory_order_relaxed);
    republishPreviewTable(snap.get());
}

void TonetraceKernel::publishNotesLocked(std::vector<NoteEvent> notes) {
    const SnapshotPtr cur = snapshot();
    if (cur) {
        publishLocked(withNotes(*cur, std::move(notes)));
        return;
    }
    Recording none;
    none.bpm = hostBpm();
    publishLocked(makeSnapshot(std::move(notes), none, 1));
}

void TonetraceKernel::setNotes(std::vector<NoteEvent> notes) {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    publishNotesLocked(std::move(notes));
}

void TonetraceKernel::checkpoint() {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    const SnapshotPtr cur = snapshot();
    history_.record(cur ? cur->notes : std::vector<NoteEvent>{});
}

void TonetraceKernel::editNotes(std::vector<NoteEvent> notes) {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    const SnapshotPtr cur = snapshot();
    history_.record(cur ? cur->notes : std::vector<NoteEvent>{});
    publishNotesLocked(std::move(notes));
}

bool TonetraceKernel::undo() {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    return applyHistoryLocked(true);
}

bool TonetraceKernel::redo() {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    return applyHistoryLocked(false);
}

// Requires mutateMutex_. A document step (Reset) swaps the whole snapshot and refills the preview audio from it.
bool TonetraceKernel::applyHistoryLocked(bool isUndo) {
    SnapshotPtr document = snapshot();
    std::vector<NoteEvent> notes = document ? document->notes : std::vector<NoteEvent>{};
    const auto applied = isUndo ? history_.undo(&notes, &document) : history_.redo(&notes, &document);
    if (applied == NoteHistory::Applied::None) return false;
    if (applied == NoteHistory::Applied::Notes) {
        publishNotesLocked(std::move(notes));
        return true;
    }
    publishLocked(document);
    restoreCaptureFromSnapshot();
    return true;
}

bool TonetraceKernel::canUndo() const {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    return history_.canUndo();
}

bool TonetraceKernel::canRedo() const {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    return history_.canRedo();
}

int TonetraceKernel::quickGrab() {
    if (status() != KernelStatus::Idle) return 0;
    const SnapshotPtr base = snapshot();
    if (!base || !base->spectrogram) return 0;
    const GrabEngine engine = lineView().isNeural ? GrabEngine::Neural : GrabEngine::Classic;
    GrabOutcome outcome = quickGrabWithEngine(*base, param(kParamQuickGrab), filterSettings(), tuningOffsetCents(), engine);
    reportFallback(outcome);
    return mergeGrabbed(base, std::move(outcome.notes));
}

// Neural was chosen but Classic ran: log it and tell the user once per grab (never an error, the notes are still there).
void TonetraceKernel::reportFallback(const GrabOutcome& outcome) {
    if (outcome.fallbackReason.empty()) return;
    logWarn(instanceId_, "neural engine unavailable (%s), Quick Grab used Classic", outcome.fallbackReason.c_str());
    notice("Neural engine unavailable (" + outcome.fallbackReason + ") - Quick Grab used Classic", NoticeLevel::Warning);
}

// Adds Quick Grab notes as one undo step; the document may have changed while the grab was computed.
int TonetraceKernel::mergeGrabbed(const SnapshotPtr& base, std::vector<NoteEvent> grabbed) {
    std::lock_guard<std::mutex> guard(mutateMutex_);
    const SnapshotPtr cur = snapshot();
    if (!cur || cur->spectrogram != base->spectrogram) return 0;  // another recording or a reset in between
    std::vector<NoteEvent> merged = cur->notes;
    merged.insert(merged.end(), grabbed.begin(), grabbed.end());
    merged = normalizedNotes(std::move(merged));
    const int added = int(merged.size()) - int(cur->notes.size());
    if (added <= 0) return 0;
    history_.record(cur->notes);
    publishLocked(withNotes(*cur, std::move(merged)));
    return added;
}

LineView TonetraceKernel::lineView() const {
    return {lineFundamentals_.load(), lineAlign_.load(), lineTrim_.load(), lineSpectrum_.load(), lineGrabOnClick_.load(), lineSamplerOnDrag_.load(), lineNeural_.load()};
}

void TonetraceKernel::setLineView(const LineView& view) {
    lineFundamentals_.store(view.fundamentals);
    lineAlign_.store(view.alignToSample);
    lineSpectrum_.store(view.isSpectrum);
    lineGrabOnClick_.store(view.grabOnClick);
    lineSamplerOnDrag_.store(view.samplerOnDrag);
    lineNeural_.store(view.isNeural);
    lineTrim_.store(std::isfinite(view.trimCents) ? std::clamp(view.trimCents, -kMaxTrimCents, kMaxTrimCents) : 0.0f);
}

double TonetraceKernel::detectedTuningCents(bool* isReliable) const {
    const SnapshotPtr snap = snapshot();
    const bool ok = snap && snap->lines && snap->lines->hasTuning();
    if (isReliable) *isReliable = ok;
    return ok ? double(snap->lines->tuningCents) : 0.0;
}

double TonetraceKernel::tuningOffsetCents() const {
    const LineView v = lineView();
    return (v.alignToSample ? detectedTuningCents() : 0.0) + double(v.trimCents);
}

void TonetraceKernel::resetAll() {
    if (status() != KernelStatus::Idle) return;
    cancelQuickGrab();
    std::lock_guard<std::mutex> guard(mutateMutex_);
    if (snapshot()) history_.recordDocument(snapshot());  // Reset can be undone
    capture_.reset();
    playhead_.store(0.0);
    previewStart_.store(0.0);
    setPreviewLoop(0.0, 0.0);
    publishLocked(nullptr);
    logInfo(instanceId_, "reset: recording and notes cleared");
}

void TonetraceKernel::restoreCaptureFromSnapshot() {
    const SnapshotPtr snap = snapshot();
    if (!snap || !snap->audio || snap->audio->samples.empty() || capture_.capacity() == 0 || status() == KernelStatus::Recording) return;
    const AudioClip& clip = *snap->audio;
    const size_t count = size_t(double(clip.samples.size()) * sampleRate_ / clip.sampleRate);
    std::vector<float> resampled(count);
    const double step = clip.sampleRate / sampleRate_;
    const size_t last = clip.samples.size() - 1;
    for (size_t i = 0; i < count; ++i) {  // linear interpolation back to the host rate
        const double pos = double(i) * step;
        const size_t k = std::min(size_t(pos), last), k1 = std::min(k + 1, last);
        const float frac = float(pos - double(k));
        resampled[i] = (float(clip.samples[k]) * (1.0f - frac) + float(clip.samples[k1]) * frac) / 32768.0f;
    }
    capture_.load(resampled.data(), resampled.size());
}

std::vector<uint8_t> TonetraceKernel::saveState() const {
    return encodeState(allParams(), snapshot().get(), syncToHost());
}

bool TonetraceKernel::loadState(const uint8_t* data, size_t size) {
    DecodedState state;
    std::string reason;
    if (!decodeState(data, size, &state, &reason)) {
        logError(instanceId_, "state load failed (%zu bytes): %s", size, reason.c_str());
        diagnostics_.setLastError("state load failed: " + reason);
        notice("The saved state could not be loaded: " + reason, NoticeLevel::Error);
        return false;
    }
    for (int i = 0; i < kNumParams; ++i) setParam(i, state.params[size_t(i)]);
    setSyncToHost(state.syncToHost);
    {
        std::lock_guard<std::mutex> guard(mutateMutex_);
        history_.clear();
        publishLocked(state.snapshot);
    }
    restoreCaptureFromSnapshot();
    startLinesJob();
    logInfo(instanceId_, "state loaded: %zu bytes, format v%u, %zu notes, %s", size, state.formatVersion,
            state.snapshot ? state.snapshot->notes.size() : size_t(0), state.snapshot && state.snapshot->hasRecording() ? "with recording" : "no recording");
    return true;
}

}  // namespace ttr
