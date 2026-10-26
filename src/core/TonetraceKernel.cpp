// TonetraceKernel: lifecycle, parameters, host state and diagnostics (non-audio threads).
#include "core/TonetraceKernel.h"

#include <mach/mach_time.h>

#include <algorithm>
#include <cmath>
#include <new>

#include "core/Logger.h"
#include "core/Version.h"

namespace ttr {

namespace {
constexpr double kMinLoopSec = 0.05;
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
}  // namespace

TonetraceKernel::TonetraceKernel() : instanceId_(nextInstanceId()) {
    const ParamValues defaults = defaultParams();
    for (int i = 0; i < kNumParams; ++i) params_[i].store(defaults[i]);
    mach_timebase_info_data_t base{};
    mach_timebase_info(&base);
    nsPerTick_ = base.denom ? double(base.numer) / double(base.denom) : 1.0;
    logInfo(instanceId_, "created Tonetrace %s (build %s)", TTR_VERSION_STRING, TTR_BUILD_HASH);
}

TonetraceKernel::~TonetraceKernel() {
    deactivate();
    logInfo(instanceId_, "destroyed");
}

void TonetraceKernel::prepare(double sampleRate, int maxFrames) {
    deactivate();
    const double newRate = sampleRate > 0 ? sampleRate : 44100.0;
    const bool rateChangedWhileRecording = status() == KernelStatus::Recording && newRate != sampleRate_;
    sampleRate_ = newRate;
    maxFrames_ = std::max(1, maxFrames);
    try {
        mono_.assign(size_t(maxFrames_), 0.0f);
        synthBuf_.assign(size_t(maxFrames_), 0.0f);
        sampleBuf_.assign(size_t(maxFrames_), 0.0f);
        capture_.prepare(sampleRate_, kMaxRecordSeconds);
        {
            std::lock_guard<std::mutex> liveLock(liveMutex_);  // the editor may be drawing from these buffers
            live_.prepare(sampleRate_, kMaxRecordSeconds);
            livePeaks_.prepare(sampleRate_, kMaxRecordSeconds);
        }
    } catch (const std::bad_alloc&) {
        logError(instanceId_, "out of memory while preparing %.0f Hz / %d frames", sampleRate_, maxFrames_);
        notice("Not enough memory for the recording buffers - recording is unavailable", NoticeLevel::Error);
        diagnostics_.setLastError("out of memory in prepare");
        return;
    }
    synth_.prepare(sampleRate_);
    resetAudioState();
    if (rateChangedWhileRecording) {
        setParam(kParamRecord, 0.0f);
        notice("Sample rate changed during the recording - the recording was discarded");
        logWarn(instanceId_, "sample rate changed to %.0f Hz while recording: recording discarded", sampleRate_);
    }
    restoreCaptureFromSnapshot();
    diagnostics_.setLayout(sampleRate_, maxFrames_, diagnostics_.snapshot().channels);
    logInfo(instanceId_, "prepared: %.0f Hz, max block %d", sampleRate_, maxFrames_);
    workerRun_.store(true);
    worker_ = std::thread([this] { workerLoop(); });
}

void TonetraceKernel::deactivate() {
    cancelQuickGrab();
    workerRun_.store(false);
    if (worker_.joinable()) worker_.join();
    drainAudioEvents();
}

void TonetraceKernel::resetAudioState() {
    synth_.allNotesOff();
    wasRecording_ = wasPreviewing_ = captureFull_ = tailPending_ = wasHostPlaying_ = false;
    playSample_ = 0;
    fadeInDone_ = 0;
    lastBlock_ = maxSeenBlock_ = 0;
    playhead_.store(0.0);
    if (status() == KernelStatus::Recording) status_.store(int(KernelStatus::Idle));
}

void TonetraceKernel::setProcessing(bool isProcessing) {
    diagnostics_.setFlags(isOffline_, isProcessing);
    logInfo(instanceId_, "setProcessing(%s)", isProcessing ? "true" : "false");
    if (isProcessing) return;
    synth_.allNotesOff();
    wasPreviewing_ = tailPending_ = wasHostPlaying_ = false;
    playSample_ = 0;
    fadeInDone_ = 0;
    playhead_.store(0.0);
    if (wasRecording_) {  // the transport stopped: a running recording ends here and gets analysed
        wasRecording_ = false;
        params_[kParamRecord].store(0.0f);
        analysisSeq_.fetch_add(1);
        status_.store(int(KernelStatus::Analysing));
        logInfo(instanceId_, "recording ended by setProcessing(false): %.2f s", recordedSeconds());
    }
    drainAudioEvents();
}

void TonetraceKernel::setOffline(bool isOffline) {
    if (isOffline == isOffline_) return;
    isOffline_ = isOffline;
    diagnostics_.setFlags(isOffline_, diagnostics_.snapshot().isProcessing);
    logInfo(instanceId_, "process mode: %s", isOffline ? "offline" : "realtime");
}

void TonetraceKernel::setHostName(const std::string& name) {
    diagnostics_.setHost(name);
    logInfo(instanceId_, "host: %s", name.empty() ? "unknown" : name.c_str());
}

void TonetraceKernel::setChannels(int channels) {
    const DiagnosticsSnapshot d = diagnostics_.snapshot();
    diagnostics_.setLayout(d.sampleRate, d.maxBlock, channels);
    logInfo(instanceId_, "bus layout: %d channel(s) in/out", channels);
}

float TonetraceKernel::param(int id) const { return params_[std::clamp(id, 0, kNumParams - 1)].load(); }

void TonetraceKernel::setParam(int id, float value) {
    if (id < 0 || id >= kNumParams || !std::isfinite(value)) return;
    const ParamSpec& spec = paramSpec(id);
    float v = std::clamp(value, spec.minValue, spec.maxValue);
    if (spec.kind != ParamKind::Continuous) v = std::round(v);
    params_[id].store(v);
}

ParamValues TonetraceKernel::allParams() const {
    ParamValues v{};
    for (int i = 0; i < kNumParams; ++i) v[i] = params_[i].load();
    return v;
}

FilterSettings TonetraceKernel::filterSettings() const {
    FilterSettings f;
    f.hideDrums = param(kParamHideDrums) >= 0.5f;
    return f;
}

void TonetraceKernel::setHostTransport(double bpm, double beat, bool isPlaying) {
    if (std::isfinite(bpm) && bpm >= 20.0 && bpm <= 400.0) hostBpm_.store(bpm, std::memory_order_relaxed);
    if (std::isfinite(beat)) hostBeat_.store(beat, std::memory_order_relaxed);
    hostPlaying_.store(isPlaying, std::memory_order_relaxed);
}

void TonetraceKernel::setPreviewLoop(double startSec, double endSec) {
    const bool valid = std::isfinite(startSec) && std::isfinite(endSec) && endSec - startSec >= kMinLoopSec;
    loopStart_.store(valid ? std::max(0.0, startSec) : 0.0);
    loopEnd_.store(valid ? endSec : 0.0);
}

bool TonetraceKernel::previewLoop(double* startSec, double* endSec) const {
    const double a = loopStart_.load(), b = loopEnd_.load();
    if (startSec) *startSec = a;
    if (endSec) *endSec = b;
    return b - a >= kMinLoopSec;
}

void TonetraceKernel::audition(int pitch) { table_.pushAudition(pitch); }

double TonetraceKernel::recordedSeconds() const { return double(capture_.size()) / sampleRate_; }

DiagnosticsSnapshot TonetraceKernel::diagnosticsSnapshot() const {
    DiagnosticsSnapshot d = diagnostics_.snapshot();
    d.instanceId = instanceId_;
    d.recordedSeconds = recordedSeconds();
    d.captureMegabytes = double(capture_.size()) * sizeof(float) / kBytesPerMegabyte;
    return d;
}

}  // namespace ttr
