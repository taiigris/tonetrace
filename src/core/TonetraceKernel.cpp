// TonetraceKernel: lifecycle, parameters, transport, mixer and MIDI export (non-audio threads).
#include "core/TonetraceKernel.h"

#include <mach/mach_time.h>

#include <algorithm>
#include <cmath>
#include <new>

#include "core/Logger.h"
#include "core/Version.h"
#include "midi/ExportPlanner.h"

namespace ttr {

namespace {
constexpr double kBytesPerMegabyte = 1024.0 * 1024.0;
constexpr double kMinBpm = 20.0, kMaxBpm = 400.0;
}  // namespace

TonetraceKernel::TonetraceKernel() : instanceId_(nextInstanceId()) {
    const ParamValues defaults = defaultParams();
    for (int i = 0; i < kNumParams; ++i) params_[i].store(defaults[i]);
    for (auto& l : levels_) l.store(0.0f);
    mach_timebase_info_data_t base{};
    mach_timebase_info(&base);
    nsPerTick_ = base.denom ? double(base.numer) / double(base.denom) : 1.0;
    logInfo(instanceId_, "created Tonetrace %s (build %s)", TTR_VERSION_STRING, TTR_BUILD_HASH);
}

TonetraceKernel::~TonetraceKernel() {
    deactivate();
    stopJob();
    logInfo(instanceId_, "destroyed");
}

void TonetraceKernel::prepare(double sampleRate, int maxFrames) {
    deactivate();
    sampleRate_ = sampleRate > 0 ? sampleRate : 44100.0;
    maxFrames_ = std::max(1, maxFrames);
    try {
        synthBuf_.assign(size_t(maxFrames_), 0.0f);
        captureL_.prepare(sampleRate_, kMaxRecordSeconds);
        captureR_.prepare(sampleRate_, kMaxRecordSeconds);
        std::lock_guard<std::mutex> lock(recordMutex_);
        recordPeaks_.prepare(sampleRate_, kMaxRecordSeconds);
    } catch (const std::bad_alloc&) {
        logError(instanceId_, "out of memory while preparing %.0f Hz / %d frames", sampleRate_, maxFrames_);
        notice("Not enough memory for the recording buffers - recording is unavailable", NoticeLevel::Error);
        diagnostics_.setLastError("out of memory in prepare");
        return;
    }
    synth_.prepare(sampleRate_);
    wasRecording_ = wasPlaying_ = false;
    firstBlock_ = true;
    diagnostics_.setLayout(sampleRate_, maxFrames_, diagnostics_.snapshot().channels);
    logInfo(instanceId_, "prepared: %.0f Hz, max block %d", sampleRate_, maxFrames_);
    workerRun_.store(true);
    worker_ = std::thread([this] { workerLoop(); });
}

void TonetraceKernel::deactivate() {
    workerRun_.store(false);
    if (worker_.joinable()) worker_.join();
    drainAudioEvents();
    collectRetiredAudio(true);  // the audio thread does not run while deactivated
}

void TonetraceKernel::setProcessing(bool isProcessing) {
    diagnostics_.setFlags(isOffline_.load(), isProcessing);
    logInfo(instanceId_, "setProcessing(%s)", isProcessing ? "true" : "false");
    if (isProcessing) return;
    synth_.reset();
    wasPlaying_ = false;
    if (wasRecording_) {  // the transport stopped: a running recording ends here and gets transcribed
        wasRecording_ = false;
        wasRecordingFlag_.store(false);
        params_[kParamRecord].store(0.0f);
        recordStops_.fetch_add(1);
        logInfo(instanceId_, "recording ended by setProcessing(false): %.2f s", recordedSeconds());
    }
    drainAudioEvents();
}

void TonetraceKernel::setOffline(bool isOffline) {
    if (isOffline == isOffline_.exchange(isOffline)) return;
    diagnostics_.setFlags(isOffline, diagnostics_.snapshot().isProcessing);
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

void TonetraceKernel::setHostTransport(double bpm, double /*beat*/, bool /*isPlaying*/) {
    if (std::isfinite(bpm) && bpm >= kMinBpm && bpm <= kMaxBpm) hostBpm_.store(bpm, std::memory_order_relaxed);
}

// ------------------------------------------------------------ transport

void TonetraceKernel::play() {
    if (docLength_.load() <= 0.0) return;
    if (positionSec_.load() >= docLength_.load()) seek(0.0);
    playing_.store(true);
}

void TonetraceKernel::pause() { playing_.store(false); }

void TonetraceKernel::togglePlay() {
    if (playing_.load()) pause();
    else play();
}

void TonetraceKernel::seek(double sec) {
    const double clamped = std::clamp(std::isfinite(sec) ? sec : 0.0, 0.0, std::max(0.0, docLength_.load()));
    positionSec_.store(clamped);
    seekRequest_.store(clamped);
}

// ------------------------------------------------------------ mixer

MixerState TonetraceKernel::mixer() const {
    std::lock_guard<std::mutex> guard(docMutex_);
    return mixer_;
}

void TonetraceKernel::setInstrumentMix(int program, const InstrumentMix& mix) {
    {
        std::lock_guard<std::mutex> guard(docMutex_);
        mixer_ = mixer_.with(program, mix);
    }
    republishPlayTable();
}

float TonetraceKernel::instrumentLevel(int program) const {
    const int slot = program + 1;
    return slot >= 0 && slot < NoteSynth::kLevelSlots ? levels_[size_t(slot)].load(std::memory_order_relaxed) : 0.0f;
}

// ------------------------------------------------------------ export

double TonetraceKernel::exportBpm() const {
    const double user = exportBpm_.load();
    return user > 0.0 ? user : hostBpm();
}

void TonetraceKernel::setExportBpm(double bpm) {
    if (!std::isfinite(bpm)) return;
    exportBpm_.store(std::clamp(bpm, kMinBpm, kMaxBpm));
}

std::vector<uint8_t> TonetraceKernel::buildMidi(bool allInstruments) const {
    const DocumentPtr doc = document();
    if (!doc) return {};
    const std::vector<NoteEvent> notes = notesForExport(doc->notes, mixer(), allInstruments);
    return ttr::buildMidi(notes, exportBpm(), songStem(doc->sourceName));
}

std::string TonetraceKernel::midiFileName() const {
    const DocumentPtr doc = document();
    return ttr::midiFileName(doc ? doc->sourceName : std::string(), exportBpm());
}

// ------------------------------------------------------------ recording view, diagnostics

double TonetraceKernel::recordedSeconds() const { return double(captureL_.size()) / sampleRate_; }

RecordingView TonetraceKernel::recordingView() const {
    std::lock_guard<std::mutex> lock(recordMutex_);
    return {recordPeaks_.data(), recordPeaks_.count(), recordPeaks_.hold()};
}

DiagnosticsSnapshot TonetraceKernel::diagnosticsSnapshot() const {
    DiagnosticsSnapshot d = diagnostics_.snapshot();
    d.instanceId = instanceId_;
    d.recordedSeconds = recordedSeconds();
    d.captureMegabytes = double(captureL_.size() + captureR_.size()) * sizeof(float) / kBytesPerMegabyte;
    return d;
}

}  // namespace ttr
