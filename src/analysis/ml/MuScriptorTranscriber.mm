#include "analysis/ml/MuScriptorTranscriber.h"

#import <CommonCrypto/CommonDigest.h>
#import <Foundation/Foundation.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <vector>

#include "analysis/ml/MuScriptorEngine.h"
#include "analysis/ml/muscriptor/MuScriptorBridge.h"

namespace ttr {

namespace {

constexpr size_t kHashChunk = 8u << 20;  // 8 MB per read while hashing

// Download state, shared between the worker thread (loader) and NSURLSession's queue.
struct Download {
    std::mutex mutex;
    NSURLSessionDownloadTask* task = nil;
    std::string error;  // last failure, cleared when a new download starts
};

Download& download() {
    static Download d;
    return d;
}

bool fileExists(const std::string& path) { return [[NSFileManager defaultManager] fileExistsAtPath:[NSString stringWithUTF8String:path.c_str()]]; }

long long fileSize(const std::string& path) {
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:[NSString stringWithUTF8String:path.c_str()] error:nil];
    return attrs ? [attrs[NSFileSize] longLongValue] : -1;
}

std::string sha256Of(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    std::vector<unsigned char> buffer(kHashChunk);
    for (size_t n; (n = std::fread(buffer.data(), 1, buffer.size(), f)) > 0;) CC_SHA256_Update(&ctx, buffer.data(), CC_LONG(n));
    std::fclose(f);
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);
    std::string hex;
    char byte[3];
    for (unsigned char b : digest) { std::snprintf(byte, sizeof byte, "%02x", b); hex += byte; }
    return hex;
}

// Moves a finished download into place after the size and checksum match; returns the error otherwise.
std::string installDownloaded(NSURL* location, const std::string& target) {
    const std::string part = target + ".part";
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* partPath = [NSString stringWithUTF8String:part.c_str()];
    [fm removeItemAtPath:partPath error:nil];
    NSError* err = nil;
    if (![fm moveItemAtURL:location toURL:[NSURL fileURLWithPath:partPath] error:&err]) return "cannot store the download: " + std::string(err.localizedDescription.UTF8String);
    if (fileSize(part) != kMuScriptorModelBytes || sha256Of(part) != kMuScriptorModelSha256) {
        [fm removeItemAtPath:partPath error:nil];
        return "the downloaded model is damaged (checksum mismatch)";
    }
    if (![fm moveItemAtPath:partPath toPath:[NSString stringWithUTF8String:target.c_str()] error:&err]) return "cannot store the model: " + std::string(err.localizedDescription.UTF8String);
    return {};
}

// Starts the download unless one is running. Thread-safe, non-blocking.
void startDownload() {
    Download& d = download();
    std::lock_guard<std::mutex> guard(d.mutex);
    if (d.task) return;
    const std::string target = muscriptorModelPath();
    NSString* dir = [[NSString stringWithUTF8String:target.c_str()] stringByDeletingLastPathComponent];
    NSError* err = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:&err]) {
        d.error = "cannot create " + std::string(dir.UTF8String);
        return;
    }
    d.error.clear();
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:kMuScriptorModelUrl]];
    d.task = [[NSURLSession sharedSession] downloadTaskWithURL:url completionHandler:^(NSURL* location, NSURLResponse* response, NSError* error) {
        std::string failure;
        const NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]] ? ((NSHTTPURLResponse*)response).statusCode : 0;
        if (error) failure = "download failed: " + std::string(error.localizedDescription.UTF8String);
        else if (status != 200) failure = "download failed (HTTP " + std::to_string(long(status)) + ")";
        else failure = installDownloaded(location, target);
        Download& state = download();
        std::lock_guard<std::mutex> lock(state.mutex);
        state.error = failure;
        state.task = nil;
    }];
    [d.task resume];
}

// Why the model is not on disk yet (progress or last error); starts the download when nothing is running.
std::string pendingReason() {
    startDownload();
    Download& d = download();
    std::lock_guard<std::mutex> guard(d.mutex);
    if (d.task) {
        const double share = d.task.progress.fractionCompleted;
        return "MuScriptor model downloading (" + std::to_string(int(share * 100.0)) + " % of 620 MB)";
    }
    return d.error.empty() ? "MuScriptor model not downloaded yet" : d.error;
}

class MuScriptorTranscriber final : public NoteTranscriber {
public:
    explicit MuScriptorTranscriber(std::shared_ptr<msb::Model> model) : model_(std::move(model)) {}
    bool transcribe(const std::vector<float>& audio, std::vector<TranscribedNote>* out, TaskControl* control, std::string* reason) override {
        std::lock_guard<std::mutex> guard(mutex_);  // one transcription at a time per model (KV cache)
        std::vector<msb::Note> notes;
        const auto cancel = [control] { return control && control->cancelled(); };
        const auto progress = [control](float p) { if (control) control->report(p); };
        if (!msb::transcribe(*model_, audio.data(), audio.size(), &notes, cancel, progress, reason)) return false;
        out->clear();
        for (const msb::Note& n : notes) out->push_back({n.onset, n.offset, n.pitch, n.isDrum});
        return true;
    }

private:
    std::mutex mutex_;
    std::shared_ptr<msb::Model> model_;
};

std::shared_ptr<NoteTranscriber> loadTranscriber(std::string* reason) {
    std::string why;
    if (!msb::isSupported()) {
        msb::load({}, &why);
        if (reason) *reason = why;
        return nullptr;
    }
    const std::string path = muscriptorModelPath();
    const bool isOverride = std::getenv(kMuScriptorModelEnv) != nullptr;
    if (!fileExists(path)) {
        if (reason) *reason = isOverride ? "model not found: " + path : pendingReason();
        return nullptr;
    }
    std::shared_ptr<msb::Model> model = msb::load(path, &why);
    if (!model) {
        if (reason) *reason = why;
        return nullptr;
    }
    return std::make_shared<MuScriptorTranscriber>(std::move(model));
}

}  // namespace

std::string muscriptorModelPath() {
    if (const char* env = std::getenv(kMuScriptorModelEnv); env && *env) return env;
    NSString* lib = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) firstObject];
    return std::string(lib.UTF8String) + "/Tonetrace/models/" + kMuScriptorModelName;
}

void installMuScriptorLoader() {
    static std::once_flag once;
    std::call_once(once, [] {
        setTranscriberLoader([](std::string* reason) { return loadTranscriber(reason); });
        setTranscriberPrefetch([]() -> std::string {
            if (!msb::isSupported()) return "MuScriptor needs an Apple Silicon Mac - Quick Grab uses Classic";
            if (std::getenv(kMuScriptorModelEnv) || fileExists(muscriptorModelPath())) return {};
            startDownload();
            return "MuScriptor: downloading the model (620 MB) in the background - Quick Grab uses Classic until it is ready";
        });
    });
}

}  // namespace ttr
