// TonetraceKernel: the document and the background work. A song is decoded, published (playable at once), then
// transcribed by MuScriptor; every finished chunk publishes the notes so far. The worker thread turns a stopped
// recording into a document the same way.
#include <malloc/malloc.h>

#include <algorithm>
#include <chrono>
#include <cstdio>

#include "analysis/ml/MuScriptorEngine.h"
#include "core/Instruments.h"
#include "core/Logger.h"
#include "core/SongAudio.h"
#include "core/SongImport.h"
#include "core/TonetraceKernel.h"

namespace ttr {

namespace {

constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr auto kModelWaitInterval = std::chrono::milliseconds(250);
constexpr double kMinRecordingSec = 0.25;
constexpr uint64_t kRetireEpochs = 2;  // the audio thread finished at least one whole block after the swap

// The loader's reasons while the model is still on its way (MuScriptorTranscriber.mm).
bool isModelPending(const std::string& reason) {
    return reason.rfind("model downloading", 0) == 0 || reason.rfind("model not downloaded yet", 0) == 0;
}

std::string minutesText(double seconds) {
    char text[16];
    const int total = int(seconds + 0.5);
    std::snprintf(text, sizeof text, "%d:%02d", total / 60, total % 60);
    return text;
}

}  // namespace

// ------------------------------------------------------------ document

DocumentPtr TonetraceKernel::document() const {
    std::lock_guard<std::mutex> guard(docMutex_);
    return doc_;
}

void TonetraceKernel::publish(DocumentPtr doc) {
    {
        std::lock_guard<std::mutex> guard(docMutex_);
        doc_ = doc;
    }
    docLength_.store(doc ? doc->lengthSec : 0.0);
    setPlayAudio(doc ? doc->audio : nullptr);
    republishPlayTable();
}

bool TonetraceKernel::updateDocument(uint64_t id, const std::function<void(Document&)>& change, uint64_t run) {
    DocumentPtr next;
    {
        std::lock_guard<std::mutex> guard(docMutex_);
        if (!doc_ || doc_->id != id || (run != kAnyRun && doc_->transcription.run != run)) return false;
        auto copy = std::make_shared<Document>(*doc_);
        change(*copy);
        copy->version = doc_->version + 1;
        next = copy;
    }
    publish(next);
    return true;
}

// The notes the audio thread plays: audible instruments only, with their fader gain, sorted by start.
void TonetraceKernel::republishPlayTable() {
    DocumentPtr doc;
    MixerState mix;
    {
        std::lock_guard<std::mutex> guard(docMutex_);
        doc = doc_;
        mix = mixer_;
    }
    std::vector<PlayNote> notes;
    if (doc) {
        notes.reserve(doc->notes.size());
        for (const NoteEvent& n : doc->notes) {
            const float gain = mix.gainOf(n.program);
            if (gain > 0.0f) notes.push_back({n.startSec, n.endSec, gain, int16_t(n.pitch), int16_t(n.program)});
        }
    }
    table_.publish(notes);
}

void TonetraceKernel::setPlayAudio(std::shared_ptr<const SongAudio> audio) {
    std::lock_guard<std::mutex> guard(audioMutex_);
    if (audio == playAudioOwner_) return;
    if (playAudioOwner_) retired_.push_back({playAudioOwner_, audioEpoch_.load(std::memory_order_acquire)});
    playAudioOwner_ = std::move(audio);
    playAudio_.store(playAudioOwner_.get(), std::memory_order_release);
}

void TonetraceKernel::collectRetiredAudio(bool force) {
    std::lock_guard<std::mutex> guard(audioMutex_);
    const uint64_t now = audioEpoch_.load(std::memory_order_acquire);
    retired_.erase(std::remove_if(retired_.begin(), retired_.end(), [&](const Retired& r) { return force || now >= r.epoch + kRetireEpochs; }),
                   retired_.end());
}

void TonetraceKernel::clearDocument() {
    stopJob();
    pause();
    seek(0.0);
    publish(nullptr);
    logInfo(instanceId_, "document cleared");
}

// ------------------------------------------------------------ jobs

// Runs `job` on the job thread. A running job is asked to stop and finishes first (on the new thread, so the caller
// never waits). Only the latest job runs; an older one that has not started yet is skipped.
void TonetraceKernel::startJob(std::function<void(TaskControl&)> job) {
    std::lock_guard<std::mutex> guard(jobMutex_);
    const uint64_t generation = ++jobGeneration_;
    jobControl_.cancel.store(true);
    std::thread previous = std::move(jobThread_);
    jobRunning_.store(true);
    jobThread_ = std::thread([this, generation, job = std::move(job), previous = std::move(previous)]() mutable {
        if (previous.joinable()) previous.join();
        if (generation != jobGeneration_.load()) return;  // replaced or stopped meanwhile
        jobRunning_.store(true);
        jobControl_.cancel.store(false);
        jobControl_.report(0.0f);
        job(jobControl_);
        malloc_zone_pressure_relief(nullptr, 0);  // decoded audio and model temporaries go back to the OS
        if (generation == jobGeneration_.load()) jobRunning_.store(false);
    });
}

void TonetraceKernel::stopJob() {
    std::thread running;
    {
        std::lock_guard<std::mutex> guard(jobMutex_);
        ++jobGeneration_;
        jobControl_.cancel.store(true);
        running = std::move(jobThread_);
    }
    if (running.joinable()) running.join();
    jobRunning_.store(false);
}

void TonetraceKernel::cancelTranscription() { jobControl_.cancel.store(true); }

std::vector<int> TonetraceKernel::instrumentSelection() const {
    std::lock_guard<std::mutex> guard(docMutex_);
    return instrumentSelection_;
}

void TonetraceKernel::setInstrumentSelection(const std::vector<int>& programs) {
    const std::vector<int> selection = normalizedSelection(programs);
    uint64_t id = 0;
    {
        std::lock_guard<std::mutex> guard(docMutex_);
        if (selection == instrumentSelection_) return;
        instrumentSelection_ = selection;
        id = doc_ ? doc_->id : 0;
    }
    logInfo(instanceId_, "instrument selection: %s", selectionSummary(selection).c_str());
    if (id != 0) updateDocument(id, [this](Document& d) { applySelection(d); });  // instant: the full analysis is kept (republishes the notes)
}

bool TonetraceKernel::modelFocus() const {
    std::lock_guard<std::mutex> guard(docMutex_);
    return isModelFocused_;
}

void TonetraceKernel::setModelFocus(bool isFocused) {
    std::lock_guard<std::mutex> guard(docMutex_);
    isModelFocused_ = isFocused;
}

std::vector<int> TonetraceKernel::listenFor() const {
    std::lock_guard<std::mutex> guard(docMutex_);
    return isModelFocused_ ? instrumentSelection_ : std::vector<int>();
}

void TonetraceKernel::applySelection(Document& d) const {
    if (d.detectedNotes.empty() && !d.notes.empty()) d.detectedNotes = d.notes;  // a document from before the selection
    d.notes = notesOfInstruments(d.detectedNotes, instrumentSelection_);
}

bool TonetraceKernel::needsReanalysis() const {
    const DocumentPtr doc = document();
    if (!doc || !doc->audio) return false;
    const TranscriptionPhase phase = doc->transcription.phase;
    return phase == TranscriptionPhase::Failed || phase == TranscriptionPhase::Cancelled || doc->transcription.listenedFor != listenFor();
}

bool TonetraceKernel::reanalyse() {
    const DocumentPtr doc = document();
    if (!doc || !doc->audio || isRecording()) return false;
    const uint64_t id = doc->id, run = ++runIds_;
    const std::shared_ptr<const SongAudio> audio = doc->audio;
    // The new run makes every update of the previous analysis (still finishing on the job thread) a no-op.
    const bool current = updateDocument(id, [run](Document& d) {
        d.notes.clear();
        d.detectedNotes.clear();
        d.transcription = {TranscriptionPhase::Running, 0.0f, 0.0, {}, run, {}};
    });
    if (!current) return false;
    logInfo(instanceId_, "re-analysing with the %s model, %s", modelSizeInfo(modelSize()).id, selectionSummary(instrumentSelection()).c_str());
    startJob([this, id, run, audio](TaskControl& control) { runTranscription(control, id, run, monoAt(*audio, kMuScriptorRate)); });
    return true;
}

bool TonetraceKernel::loadSong(const std::string& path) {
    if (!isSongFile(path) || isRecording()) return false;
    const uint64_t id = nextDocumentId();
    auto doc = std::make_shared<Document>();
    doc->id = id;
    doc->version = 1;
    doc->sourceName = fileNameOf(path);
    doc->sourcePath = path;
    doc->transcription.phase = TranscriptionPhase::Loading;
    pause();
    seek(0.0);
    publish(doc);
    logInfo(instanceId_, "loading song: %s", doc->sourceName.c_str());
    startJob([this, id, path](TaskControl& control) { runLoadSong(control, id, path); });
    return true;
}

void TonetraceKernel::runLoadSong(TaskControl& control, uint64_t id, const std::string& path) {
    DecodedAudio decoded;
    std::string reason;
    if (!decodeAudioFile(path, &decoded, &reason)) {
        logWarn(instanceId_, "song could not be loaded: %s", reason.c_str());
        notice("\"" + fileNameOf(path) + "\" could not be loaded: " + reason, NoticeLevel::Error);
        updateDocument(id, [&](Document& d) { d.transcription = {TranscriptionPhase::Failed, 0.0f, 0.0, reason}; });
        return;
    }
    if (control.cancelled()) return;
    const std::shared_ptr<const SongAudio> audio = makeSongAudio(std::move(decoded.samples), decoded.channels, decoded.sampleRate);
    const std::shared_ptr<const WavePeaks> peaks = computePeaks(*audio);
    if (decoded.truncated)
        notice("\"" + fileNameOf(path) + "\" is " + minutesText(decoded.fileSeconds) + " long - only the first " + minutesText(kMaxSongSeconds) + " are used");
    const uint64_t run = ++runIds_;
    const bool current = updateDocument(id, [&](Document& d) {
        d.audio = audio;
        d.peaks = peaks;
        d.lengthSec = audio->seconds();
        d.transcription.phase = TranscriptionPhase::Running;
        d.transcription.run = run;
    });
    if (!current) return;
    logInfo(instanceId_, "song decoded: %.1f s, %d channel(s) - playable, transcribing", audio->seconds(), audio->channels);
    runTranscription(control, id, run, monoAt(*audio, kMuScriptorRate));
}

// Waits while the model downloads (the document shows why); false with the reason when it cannot come.
bool TonetraceKernel::waitForModel(TaskControl& control, uint64_t id, uint64_t run, std::string* reason) {
    const std::string hint = prefetchTranscriber();  // starts the download if needed
    if (!hint.empty()) notice(hint, NoticeLevel::Info);
    for (;;) {
        std::string why;
        if (noteTranscriber(&why)) return true;
        if (!isModelPending(why) || control.cancelled()) {
            *reason = control.cancelled() ? "cancelled" : why;
            return false;
        }
        if (!updateDocument(id, [&](Document& d) { d.transcription.message = why; }, run)) {
            *reason = "cancelled";
            return false;
        }
        std::this_thread::sleep_for(kModelWaitInterval);
    }
}

void TonetraceKernel::runTranscription(TaskControl& control, uint64_t id, uint64_t run, std::vector<float> audio16k) {
    const auto started = std::chrono::steady_clock::now();
    const std::vector<int> programs = listenFor();
    updateDocument(id, [&](Document& d) { d.transcription.listenedFor = programs; }, run);
    std::string reason;
    bool ok = waitForModel(control, id, run, &reason);
    if (ok) {
        updateDocument(id, [](Document& d) { d.transcription.message.clear(); }, run);
        const SongProgress onProgress = [&](const std::vector<NoteEvent>& notes, double finalizedSec, float progress) {
            updateDocument(id, [&](Document& d) {
                d.detectedNotes = notes;
                applySelection(d);
                d.transcription.finalizedSec = finalizedSec;
                d.transcription.progress = progress;
            }, run);
        };
        ok = transcribeSong(audio16k, exportBpm(), programs, &control, onProgress, &reason);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    const bool cancelled = control.cancelled() || reason == "cancelled";
    updateDocument(id, [&](Document& d) {
        if (ok) d.transcription = {TranscriptionPhase::Done, 1.0f, d.lengthSec, {}, run, programs};
        else if (cancelled) d.transcription.phase = TranscriptionPhase::Cancelled;
        else d.transcription = {TranscriptionPhase::Failed, d.transcription.progress, d.transcription.finalizedSec, reason, run, programs};
    }, run);
    const DocumentPtr doc = document();
    const bool isOurs = doc && doc->id == id && doc->transcription.run == run;
    const size_t notes = isOurs ? doc->notes.size() : 0;
    if (ok) {
        logInfo(instanceId_, "transcription done: %zu notes, %zu instruments in %.1f s", notes, isOurs ? instrumentsIn(doc->notes).size() : size_t(0), secs);
    } else if (cancelled) {
        logInfo(instanceId_, "transcription cancelled after %.1f s (%zu notes kept)", secs, notes);
    } else {
        logWarn(instanceId_, "transcription failed: %s", reason.c_str());
        notice("Transcription unavailable: " + reason, NoticeLevel::Warning);
    }
}

// A restored song: only the audio comes back (the notes are in the state).
void TonetraceKernel::runReloadAudio(TaskControl& control, uint64_t id, const std::string& path) {
    DecodedAudio decoded;
    std::string reason;
    if (!decodeAudioFile(path, &decoded, &reason)) {
        logWarn(instanceId_, "song audio not reloaded (%s): %s", path.c_str(), reason.c_str());
        notice("The song file is missing or unreadable (" + fileNameOf(path) + ") - the notes are kept, the original audio is silent", NoticeLevel::Warning);
        return;
    }
    if (control.cancelled()) return;
    const std::shared_ptr<const SongAudio> audio = makeSongAudio(std::move(decoded.samples), decoded.channels, decoded.sampleRate);
    const std::shared_ptr<const WavePeaks> peaks = computePeaks(*audio);
    updateDocument(id, [&](Document& d) {
        d.audio = audio;
        d.peaks = peaks;
        d.lengthSec = std::max(d.lengthSec, audio->seconds());
    });
}

// ------------------------------------------------------------ worker: audio events, recording

void TonetraceKernel::drainAudioEvents() {
    std::lock_guard<std::mutex> lock(eventMutex_);
    AudioEvent e;
    while (events_.pop(&e)) {
        switch (e.code) {
            case AudioEventCode::FirstBlock: logInfo(instanceId_, "first block: %d frames, %d channel(s)", e.a, e.b); break;
            case AudioEventCode::RecordStart: {
                logInfo(instanceId_, "record start at %d Hz", e.a);
                std::lock_guard<std::mutex> peaksLock(recordMutex_);
                recordPeaks_.reset();
                break;
            }
            case AudioEventCode::RecordStop: logInfo(instanceId_, "record stop: %.2f s (%d samples)", e.value, e.a); break;
            case AudioEventCode::CaptureFull:
                logWarn(instanceId_, "recording buffer full at %d samples", e.a);
                notice("Recording buffer full (2:00 maximum) - the recording stopped and is transcribed now", NoticeLevel::Warning);
                break;
            default: break;
        }
    }
}

void TonetraceKernel::workerLoop() {
    uint64_t handledStops = recordStops_.load();
    while (workerRun_.load()) {
        std::this_thread::sleep_for(kPollInterval);
        drainAudioEvents();
        if (isRecording()) {
            std::lock_guard<std::mutex> lock(recordMutex_);
            recordPeaks_.advance(captureL_.data(), captureL_.size());
        }
        if (recordStops_.load() != handledStops) {
            handledStops = recordStops_.load();
            drainAudioEvents();
            finishRecording();
        }
        collectRetiredAudio(false);
    }
}

// The captured input becomes the document (stereo, host rate) and is transcribed like a song.
void TonetraceKernel::finishRecording() {
    const size_t frames = std::min(captureL_.size(), captureR_.size());
    if (double(frames) / sampleRate_ < kMinRecordingSec) {
        notice("The recording is too short - nothing to transcribe", NoticeLevel::Warning);
        return;
    }
    std::shared_ptr<const SongAudio> audio = makeSongAudio(captureL_.data(), captureR_.data(), frames, sampleRate_);
    auto doc = std::make_shared<Document>();
    doc->id = nextDocumentId();
    doc->version = 1;
    doc->sourceName = "Recording";
    doc->isRecording = true;
    doc->audio = audio;
    doc->peaks = computePeaks(*audio);
    doc->lengthSec = audio->seconds();
    doc->transcription.phase = TranscriptionPhase::Running;
    doc->transcription.run = ++runIds_;
    seek(0.0);
    publish(doc);
    logInfo(instanceId_, "recording became the document: %.1f s", doc->lengthSec);
    const uint64_t id = doc->id, run = doc->transcription.run;
    startJob([this, id, run, audio](TaskControl& control) { runTranscription(control, id, run, monoAt(*audio, kMuScriptorRate)); });
}

}  // namespace ttr
