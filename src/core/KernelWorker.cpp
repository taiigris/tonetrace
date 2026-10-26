// TonetraceKernel: background threads (live spectrogram and waveform while recording, full analysis after,
// audio-event logging) and the Quick Grab task.
#include <malloc/malloc.h>

#include <algorithm>
#include <chrono>
#include <cmath>

#include "analysis/QuickGrab.h"
#include "analysis/Spectrogram.h"
#include "core/Logger.h"
#include "core/TonetraceKernel.h"

namespace ttr {

namespace {
constexpr auto kPollInterval = std::chrono::milliseconds(10);
constexpr double kMaxStoredAudioSeconds = 60.0;
constexpr float kInt16Scale = 32767.0f;
constexpr double kMillisPerSecond = 1000.0;

std::shared_ptr<const AudioClip> makeClip(const float* samples, size_t count, double sampleRate) {
    if (double(count) / sampleRate > kMaxStoredAudioSeconds) return nullptr;
    const DecimatedSignal reduced = decimate(samples, count, sampleRate);
    auto clip = std::make_shared<AudioClip>();
    clip->sampleRate = reduced.sampleRate;
    clip->samples.reserve(reduced.samples.size());
    for (float v : reduced.samples) clip->samples.push_back(int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * kInt16Scale)));
    return clip;
}
}  // namespace

void TonetraceKernel::workerLoop() {
    uint64_t handled = analysisSeq_.load();
    liveSeq_ = recordSeq_.load();
    while (workerRun_.load()) {
        std::this_thread::sleep_for(kPollInterval);
        drainAudioEvents();
        if (analysisSeq_.load() != handled) {
            handled = analysisSeq_.load();
            runAnalysis();
            continue;
        }
        if (status() == KernelStatus::Recording) advanceLive();
    }
}

// Turns the audio thread's events into log lines and banners. Called by the worker and after it stopped.
void TonetraceKernel::drainAudioEvents() {
    std::lock_guard<std::mutex> lock(eventMutex_);
    AudioEvent e;
    while (events_.pop(&e)) {
        switch (e.code) {
            case AudioEventCode::FirstBlock: logInfo(instanceId_, "first block: %d frames, %d channel(s)", e.a, e.b); break;
            case AudioEventCode::BlockSize: logInfo(instanceId_, "larger block: %d frames (was %d)", e.a, e.b); break;
            case AudioEventCode::RecordStart: logInfo(instanceId_, "record start at %d Hz, host beat %.3f", e.a, e.value); break;
            case AudioEventCode::RecordStop: logInfo(instanceId_, "record stop: %.2f s (%d samples)", e.value, e.a); break;
            case AudioEventCode::CaptureFull:
                logWarn(instanceId_, "recording buffer full at %d samples", e.a);
                notice("Recording buffer full (2:00 maximum) - the rest is not captured", NoticeLevel::Warning);
                break;
            default: break;
        }
    }
}

void TonetraceKernel::advanceLive() {
    const uint64_t seq = recordSeq_.load();
    if (seq != liveSeq_) {  // a new recording started: begin with empty live buffers
        liveSeq_ = seq;
        generation_.fetch_add(1);
        live_.reset();
        livePeaks_.reset();
    }
    const size_t available = capture_.size();
    live_.advance(capture_.data(), available);
    livePeaks_.advance(capture_.data(), available);
    const double analysed = double(live_.frames()) * live_.hopSec();
    diagnostics_.setWorkerDelayMs(std::max(0.0, double(available) / sampleRate_ - analysed) * kMillisPerSecond);
}

// Reads the capture buffer in place: the audio thread only writes beyond `count`, and a new recording that
// overwrites it meanwhile makes the result obsolete (checked via recordSeq_ before it is published).
void TonetraceKernel::runAnalysis() {
    const auto started = std::chrono::steady_clock::now();
    const uint64_t seq = recordSeq_.load();
    const size_t count = capture_.size();
    const float* samples = capture_.data();

    Recording recording;
    recording.lengthSec = double(count) / sampleRate_;
    recording.bpm = recBpm_;
    recording.startPpq = recBeat_;
    recording.spectrogram = computeSpectrogram(samples, count, sampleRate_);
    PeakBuilder peaks;
    peaks.prepare(sampleRate_, recording.lengthSec);
    peaks.advance(samples, count);
    recording.peaks = peaks.finish();
    recording.audio = recording.spectrogram ? makeClip(samples, count, sampleRate_) : nullptr;
    if (recording.spectrogram) recording.chroma = spectrogramChroma(*recording.spectrogram);
    if (recordSeq_.load() != seq) return;

    {
        std::lock_guard<std::mutex> guard(mutateMutex_);
        if (recordSeq_.load() != seq) return;
        publishLocked(withRecording(snapshot().get(), recording));
    }
    malloc_zone_pressure_relief(nullptr, 0);  // the analysis temporaries (tens of MB) go back to the OS
    int expected = int(KernelStatus::Analysing);
    status_.compare_exchange_strong(expected, int(KernelStatus::Idle));
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    logInfo(instanceId_, "analysis: %.2f s of audio in %.0f ms (real-time factor %.3f), %s", recording.lengthSec, ms,
            recording.lengthSec > 0 ? ms / kMillisPerSecond / recording.lengthSec : 0.0, recording.spectrogram ? "ok" : "no usable signal");
    if (!recording.spectrogram) notice("The recording is empty or too quiet - nothing to show");
}

bool TonetraceKernel::startQuickGrab() {
    if (status() != KernelStatus::Idle) return false;
    const SnapshotPtr base = snapshot();
    if (!base || !base->spectrogram) return false;
    bool expected = false;
    if (!grabRunning_.compare_exchange_strong(expected, true)) return false;
    if (grabThread_.joinable()) grabThread_.join();
    grabControl_.cancel.store(false);
    grabControl_.report(0.0f);
    const float amount = param(kParamQuickGrab);
    const FilterSettings filter = filterSettings();
    grabThread_ = std::thread([this, base, amount, filter] {
        const auto started = std::chrono::steady_clock::now();
        std::vector<NoteEvent> grabbed = quickGrabNotes(*base->spectrogram, amount, filter, &grabControl_);
        const bool cancelled = grabControl_.cancelled();
        const int added = cancelled ? 0 : mergeGrabbed(base, std::move(grabbed));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        logInfo(instanceId_, "quick grab %s: %d notes in %.0f ms", cancelled ? "cancelled" : "done", added, ms);
        if (!cancelled) notice(added > 0 ? "Quick Grab added " + std::to_string(added) + " notes - edit them like any other (Undo reverts it)"
                                         : "Quick Grab found nothing new - raise the slider or draw the notes yourself",
                            NoticeLevel::Info);
        grabRunning_.store(false);
    });
    return true;
}

void TonetraceKernel::cancelQuickGrab() {
    grabControl_.cancel.store(true);
    if (grabThread_.joinable()) grabThread_.join();
    grabRunning_.store(false);
}

}  // namespace ttr
