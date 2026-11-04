// Run-time statistics and the text report of one plug-in instance. The audio thread only touches atomics.
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace ttr {

enum class NoticeLevel { Info, Warning, Error };

struct Notice {
    std::string text;
    NoticeLevel level = NoticeLevel::Warning;
};

struct DiagnosticsSnapshot {
    int instanceId = 0;
    std::string host;           // IHostApplication::getName, empty = unknown
    double sampleRate = 0.0;
    int maxBlock = 0, lastBlock = 0, channels = 0;
    bool isOffline = false, isProcessing = false;
    double dspAvgPercent = 0.0, dspPeakPercent = 0.0;  // of the block budget
    uint64_t blocks = 0, overruns = 0;
    double workerDelayMs = 0.0;
    double recordedSeconds = 0.0;
    double captureMegabytes = 0.0;
    std::string lastMidiPath, lastError;
    std::string muscriptorEngine;  // transcription model: "not loaded yet", "ready" or why not (incl. download progress)
    // UI side (filled by the view)
    double uiFps = 0.0, uiFrameMs = 0.0;
};

class Diagnostics {
public:
    // ---- audio thread: atomics only ----
    void recordBlock(int frames, double elapsedNs, double budgetNs);

    // ---- any other thread ----
    void reset();  // clears load statistics (peak, overruns, average)
    void setHost(const std::string& host);
    void setLastMidiPath(const std::string& path);
    void setLastError(const std::string& error);
    void setLayout(double sampleRate, int maxBlock, int channels);
    void setFlags(bool isOffline, bool isProcessing);
    void setWorkerDelayMs(double ms) { workerDelayMs_.store(ms, std::memory_order_relaxed); }
    // One-line banners for the UI ("recording buffer full", "state could not be loaded: ...").
    void pushNotice(const std::string& text, NoticeLevel level = NoticeLevel::Warning);
    bool popNotice(Notice* notice);
    DiagnosticsSnapshot snapshot() const;

private:
    std::atomic<double> avgLoad_{0.0}, peakLoad_{0.0}, workerDelayMs_{0.0};
    std::atomic<uint64_t> blocks_{0}, overruns_{0};
    std::atomic<int> lastBlock_{0};
    mutable std::mutex mutex_;
    std::string host_, lastMidiPath_, lastError_;
    double sampleRate_ = 0.0;
    int maxBlock_ = 0, channels_ = 0;
    bool isOffline_ = false, isProcessing_ = false;
    std::deque<Notice> notices_;
};

// Multi-line plain-text report (version, build, host, load, ...) for "Copy report" / "Save report".
std::string formatReport(const DiagnosticsSnapshot& d);

}  // namespace ttr
