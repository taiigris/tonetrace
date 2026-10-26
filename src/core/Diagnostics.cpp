#include "core/Diagnostics.h"

#include <algorithm>
#include <cstdio>

#include "core/Version.h"

namespace ttr {

namespace {
constexpr double kAverageWeight = 0.01;  // exponential average over ~100 blocks
constexpr size_t kMaxNotices = 8;
}  // namespace

void Diagnostics::recordBlock(int frames, double elapsedNs, double budgetNs) {
    const double load = budgetNs > 0.0 ? elapsedNs / budgetNs : 0.0;
    const double avg = avgLoad_.load(std::memory_order_relaxed);
    avgLoad_.store(avg + (load - avg) * kAverageWeight, std::memory_order_relaxed);
    if (load > peakLoad_.load(std::memory_order_relaxed)) peakLoad_.store(load, std::memory_order_relaxed);
    if (load > 1.0) overruns_.fetch_add(1, std::memory_order_relaxed);
    blocks_.fetch_add(1, std::memory_order_relaxed);
    lastBlock_.store(frames, std::memory_order_relaxed);
}

void Diagnostics::reset() {
    avgLoad_.store(0.0);
    peakLoad_.store(0.0);
    overruns_.store(0);
    blocks_.store(0);
}

void Diagnostics::setHost(const std::string& host) { std::lock_guard<std::mutex> l(mutex_); host_ = host; }
void Diagnostics::setLastMidiPath(const std::string& path) { std::lock_guard<std::mutex> l(mutex_); lastMidiPath_ = path; }
void Diagnostics::setLastError(const std::string& error) { std::lock_guard<std::mutex> l(mutex_); lastError_ = error; }

void Diagnostics::setLayout(double sampleRate, int maxBlock, int channels) {
    std::lock_guard<std::mutex> l(mutex_);
    sampleRate_ = sampleRate;
    maxBlock_ = maxBlock;
    channels_ = channels;
}

void Diagnostics::setFlags(bool isOffline, bool isProcessing) {
    std::lock_guard<std::mutex> l(mutex_);
    isOffline_ = isOffline;
    isProcessing_ = isProcessing;
}

void Diagnostics::pushNotice(const std::string& text, NoticeLevel level) {
    std::lock_guard<std::mutex> l(mutex_);
    if (notices_.size() >= kMaxNotices) notices_.pop_front();
    notices_.push_back({text, level});
}

bool Diagnostics::popNotice(Notice* notice) {
    std::lock_guard<std::mutex> l(mutex_);
    if (notices_.empty()) return false;
    *notice = notices_.front();
    notices_.pop_front();
    return true;
}

DiagnosticsSnapshot Diagnostics::snapshot() const {
    DiagnosticsSnapshot d;
    d.dspAvgPercent = avgLoad_.load(std::memory_order_relaxed) * 100.0;
    d.dspPeakPercent = peakLoad_.load(std::memory_order_relaxed) * 100.0;
    d.blocks = blocks_.load(std::memory_order_relaxed);
    d.overruns = overruns_.load(std::memory_order_relaxed);
    d.lastBlock = lastBlock_.load(std::memory_order_relaxed);
    d.workerDelayMs = workerDelayMs_.load(std::memory_order_relaxed);
    std::lock_guard<std::mutex> l(mutex_);
    d.host = host_;
    d.lastMidiPath = lastMidiPath_;
    d.lastError = lastError_;
    d.sampleRate = sampleRate_;
    d.maxBlock = maxBlock_;
    d.channels = channels_;
    d.isOffline = isOffline_;
    d.isProcessing = isProcessing_;
    return d;
}

std::string formatReport(const DiagnosticsSnapshot& d) {
    char text[1536];
    snprintf(text, sizeof text,
             "Tonetrace %s (build %s)\n"
             "Instance: #%d\n"
             "Host: %s\n"
             "Sample rate: %.0f Hz\n"
             "Block size: last %d, max %d, channels %d\n"
             "Mode: %s, processing %s\n"
             "DSP load: avg %.2f %%, peak %.2f %% of the block budget\n"
             "Blocks: %llu, overruns: %llu\n"
             "Worker delay: %.1f ms\n"
             "Recording: %.1f s, capture memory %.1f MB\n"
             "UI: %.0f fps, frame %.2f ms\n"
             "Last MIDI file: %s\n"
             "Last error: %s\n",
             TTR_VERSION_STRING, TTR_BUILD_HASH, d.instanceId, d.host.empty() ? "unknown" : d.host.c_str(), d.sampleRate, d.lastBlock, d.maxBlock,
             d.channels, d.isOffline ? "offline" : "realtime", d.isProcessing ? "on" : "off", d.dspAvgPercent, d.dspPeakPercent,
             (unsigned long long)d.blocks, (unsigned long long)d.overruns, d.workerDelayMs, d.recordedSeconds, d.captureMegabytes, d.uiFps, d.uiFrameMs,
             d.lastMidiPath.empty() ? "-" : d.lastMidiPath.c_str(), d.lastError.empty() ? "-" : d.lastError.c_str());
    return text;
}

}  // namespace ttr
