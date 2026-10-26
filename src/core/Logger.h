// Rotating text log (~/Library/Logs/Tonetrace/Tonetrace.log). Thread-safe, but never called from the audio
// thread: the audio thread only bumps atomics and pushes events (see AudioEvents.h); the worker writes them.
#pragma once
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ttr {

enum class LogLevel { Info, Warn, Error };

class Logger {
public:
    static Logger& instance();

    // Empty directory = ~/Library/Logs/Tonetrace. Rotation: `files` files of at most `maxBytes` each.
    void configure(const std::string& directory, size_t maxBytes, int files);
    void log(LogLevel level, int instanceId, const std::string& message);
    std::string directory() const;
    std::string logPath() const;
    // Test support: threads that called log() so far.
    std::vector<std::thread::id> writerThreads() const;

private:
    Logger() = default;
    bool openLocked();
    void rotateLocked();
    void closeLocked();

    mutable std::mutex mutex_;
    std::string dir_;
    size_t maxBytes_ = 1024 * 1024;
    int files_ = 3;
    int fd_ = -1;
    size_t size_ = 0;
    bool failed_ = false;
    std::vector<std::thread::id> writers_;
};

int nextInstanceId();
void logInfo(int instanceId, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void logWarn(int instanceId, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void logError(int instanceId, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

}  // namespace ttr
