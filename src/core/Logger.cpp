#include "core/Logger.h"

#include <fcntl.h>
#include <cerrno>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdlib>
#include <ctime>

namespace ttr {

namespace {

constexpr size_t kMaxLineBytes = 2048;
constexpr size_t kMaxWriterIds = 64;

std::string homeDirectory() {
    if (const char* home = std::getenv("HOME")) if (*home) return home;
    if (const passwd* pw = getpwuid(getuid())) return pw->pw_dir;
    return "/tmp";
}

bool makeDirectories(const std::string& path) {
    for (size_t pos = 1; pos <= path.size(); ++pos) {
        if (pos != path.size() && path[pos] != '/') continue;
        const std::string part = path.substr(0, pos);
        if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) return false;
    }
    return true;
}

std::string timestamp() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    tm local{};
    localtime_r(&ts.tv_sec, &local);
    char text[48];
    const size_t n = strftime(text, sizeof text, "%Y-%m-%dT%H:%M:%S", &local);
    snprintf(text + n, sizeof text - n, ".%03d", int(ts.tv_nsec / 1000000));
    return text;
}

const char* levelName(LogLevel l) { return l == LogLevel::Info ? "INFO " : l == LogLevel::Warn ? "WARN " : "ERROR"; }

}  // namespace

Logger& Logger::instance() {
    static Logger* logger = new Logger;  // never destroyed: kernels may still log while the process unloads the plug-in
    return *logger;
}

void Logger::configure(const std::string& directory, size_t maxBytes, int files) {
    std::lock_guard<std::mutex> lock(mutex_);
    closeLocked();
    dir_ = directory;
    maxBytes_ = std::max<size_t>(256, maxBytes);
    files_ = std::max(1, files);
    failed_ = false;
    writers_.clear();
}

std::string Logger::directory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dir_.empty() ? homeDirectory() + "/Library/Logs/Tonetrace" : dir_;
}

std::string Logger::logPath() const { return directory() + "/Tonetrace.log"; }

std::vector<std::thread::id> Logger::writerThreads() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writers_;
}

void Logger::closeLocked() {
    if (fd_ >= 0) close(fd_);
    fd_ = -1;
    size_ = 0;
}

bool Logger::openLocked() {
    if (fd_ >= 0) return true;
    if (failed_) return false;
    const std::string dir = dir_.empty() ? homeDirectory() + "/Library/Logs/Tonetrace" : dir_;
    if (!makeDirectories(dir)) { failed_ = true; return false; }
    fd_ = open((dir + "/Tonetrace.log").c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
    if (fd_ < 0) { failed_ = true; return false; }
    struct stat st{};
    size_ = fstat(fd_, &st) == 0 ? size_t(st.st_size) : 0;
    return true;
}

// Tonetrace.log -> Tonetrace.1.log -> Tonetrace.2.log ...; the oldest one is dropped.
void Logger::rotateLocked() {
    closeLocked();
    const std::string dir = dir_.empty() ? homeDirectory() + "/Library/Logs/Tonetrace" : dir_;
    auto name = [&](int i) { return dir + (i == 0 ? "/Tonetrace.log" : "/Tonetrace." + std::to_string(i) + ".log"); };
    unlink(name(files_ - 1).c_str());
    for (int i = files_ - 2; i >= 0; --i) rename(name(i).c_str(), name(i + 1).c_str());
}

void Logger::log(LogLevel level, int instanceId, const std::string& message) {
    std::string line = timestamp() + " [#" + std::to_string(instanceId) + "] " + levelName(level) + " " + message;
    if (line.size() > kMaxLineBytes) line.resize(kMaxLineBytes);
    line.push_back('\n');
    std::lock_guard<std::mutex> lock(mutex_);
    const std::thread::id me = std::this_thread::get_id();
    if (writers_.size() < kMaxWriterIds && std::find(writers_.begin(), writers_.end(), me) == writers_.end()) writers_.push_back(me);
    if (!openLocked()) return;
    if (size_ > 0 && size_ + line.size() > maxBytes_) {
        rotateLocked();
        if (!openLocked()) return;
    }
    const ssize_t written = write(fd_, line.data(), line.size());
    if (written > 0) size_ += size_t(written);
}

int nextInstanceId() {
    static std::atomic<int> counter{0};
    return ++counter;
}

namespace {
void logv(LogLevel level, int instanceId, const char* fmt, va_list args) {
    char text[kMaxLineBytes];
    vsnprintf(text, sizeof text, fmt, args);
    Logger::instance().log(level, instanceId, text);
}
}  // namespace

void logInfo(int id, const char* fmt, ...) { va_list a; va_start(a, fmt); logv(LogLevel::Info, id, fmt, a); va_end(a); }
void logWarn(int id, const char* fmt, ...) { va_list a; va_start(a, fmt); logv(LogLevel::Warn, id, fmt, a); va_end(a); }
void logError(int id, const char* fmt, ...) { va_list a; va_start(a, fmt); logv(LogLevel::Error, id, fmt, a); va_end(a); }

}  // namespace ttr
