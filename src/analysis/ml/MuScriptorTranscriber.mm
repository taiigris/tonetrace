#include "analysis/ml/MuScriptorTranscriber.h"

#import <CommonCrypto/CommonDigest.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <vector>

#include "analysis/ml/muscriptor/MuScriptorBridge.h"

namespace ttr {

namespace {

constexpr size_t kHashChunk = 8u << 20;  // 8 MB per read while hashing
constexpr const char* kModelBaseUrl = "https://huggingface.co/DamRsn/muscriptor-gguf/resolve/main/v1/";

constexpr ModelFile kModelFiles[kNumModelSizes] = {
    {"muscriptor-small-f16.gguf", "925f55af65a20ebc4f8b45ceaf095a12b72493d436cb112623cd0041a1af23d4", 209425152},
    {"muscriptor-medium-f16.gguf", "3850cc9e5b436b17a09bd25b8f2615cb3366ab96a71e7b50f73a793a917fdf03", 618442496},
    {"muscriptor-large-f16.gguf", "35a750fb1ab1e77195cdc2c0b9b4aeea2f4d59f11f729f02af9920c4854ef72e", 2739142176},
};

// Download state per model size, shared between the worker thread (loader) and NSURLSession's queue.
struct Download {
    std::mutex mutex;
    NSURLSessionDownloadTask* task = nil;
    std::string error;  // last failure, cleared when a new download starts
};

Download& download(ModelSize size) {
    static std::array<Download, kNumModelSizes> downloads;
    return downloads[size_t(size)];
}

std::string libraryFolder() {
    NSString* lib = [NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES) firstObject];
    return std::string(lib.UTF8String) + "/Tonetrace";
}

std::string choiceFile() { return libraryFolder() + "/model-size"; }

void storeChoice(ModelSize size) {
    [[NSFileManager defaultManager] createDirectoryAtPath:[NSString stringWithUTF8String:libraryFolder().c_str()] withIntermediateDirectories:YES
                                               attributes:nil error:nil];
    std::ofstream(choiceFile()) << modelSizeInfo(size).id << "\n";
}

bool readChoice(ModelSize* size) {
    std::string id;
    std::ifstream(choiceFile()) >> id;
    return parseModelSize(id, size);
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
std::string installDownloaded(NSURL* location, const std::string& target, const ModelFile& file) {
    const std::string part = target + ".part";
    NSFileManager* fm = [NSFileManager defaultManager];
    NSString* partPath = [NSString stringWithUTF8String:part.c_str()];
    [fm removeItemAtPath:partPath error:nil];
    NSError* err = nil;
    if (![fm moveItemAtURL:location toURL:[NSURL fileURLWithPath:partPath] error:&err]) return "cannot store the download: " + std::string(err.localizedDescription.UTF8String);
    if (fileSize(part) != file.bytes || sha256Of(part) != file.sha256) {
        [fm removeItemAtPath:partPath error:nil];
        return "the downloaded model is damaged (checksum mismatch)";
    }
    if (![fm moveItemAtPath:partPath toPath:[NSString stringWithUTF8String:target.c_str()] error:&err]) return "cannot store the model: " + std::string(err.localizedDescription.UTF8String);
    return {};
}

// Starts the download of one size unless it is running. Thread-safe, non-blocking.
void startDownload(ModelSize size) {
    Download& d = download(size);
    std::lock_guard<std::mutex> guard(d.mutex);
    if (d.task) return;
    const std::string target = muscriptorModelPath(size);
    NSString* dir = [[NSString stringWithUTF8String:target.c_str()] stringByDeletingLastPathComponent];
    NSError* err = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:&err]) {
        d.error = "cannot create " + std::string(dir.UTF8String);
        return;
    }
    d.error.clear();
    const ModelFile& file = modelFile(size);
    NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:(std::string(kModelBaseUrl) + file.name).c_str()]];
    d.task = [[NSURLSession sharedSession] downloadTaskWithURL:url completionHandler:^(NSURL* location, NSURLResponse* response, NSError* error) {
        std::string failure;
        const NSInteger status = [response isKindOfClass:[NSHTTPURLResponse class]] ? ((NSHTTPURLResponse*)response).statusCode : 0;
        if (error) failure = "download failed: " + std::string(error.localizedDescription.UTF8String);
        else if (status != 200) failure = "download failed (HTTP " + std::to_string(long(status)) + ")";
        else failure = installDownloaded(location, target, file);
        Download& state = download(size);
        std::lock_guard<std::mutex> lock(state.mutex);
        state.error = failure;
        state.task = nil;
    }];
    [d.task resume];
}

// Why the model is not on disk yet (progress or last error); starts the download when nothing is running.
std::string pendingReason(ModelSize size) {
    startDownload(size);
    Download& d = download(size);
    std::lock_guard<std::mutex> guard(d.mutex);
    if (d.task) {
        const double share = d.task.progress.fractionCompleted;
        return "model downloading (" + std::to_string(int(share * 100.0)) + " % of " + modelSizeInfo(size).downloadText + ")";
    }
    return d.error.empty() ? "model not downloaded yet" : d.error;
}

class MuScriptorTranscriber final : public NoteTranscriber {
public:
    explicit MuScriptorTranscriber(std::shared_ptr<msb::Model> model) : model_(std::move(model)) {}
    bool transcribe(const std::vector<float>& audio, const std::vector<int>& programs, const ChunkCallback& onChunk, TaskControl* control,
                    std::string* reason) override {
        std::lock_guard<std::mutex> guard(mutex_);  // one transcription at a time per model (KV cache)
        std::vector<msb::Note> all;
        const auto cancel = [control] { return control && control->cancelled(); };
        TranscriptionChunk chunk;
        const msb::UpdateCallback onUpdate = [&](float progress, double finalizedSec, const std::vector<msb::Note>& fresh) {
            chunk.newNotes.clear();
            for (const msb::Note& n : fresh) chunk.newNotes.push_back({n.onset, n.offset, n.pitch, n.isDrum, n.program});
            chunk.finalizedSec = finalizedSec;
            chunk.progress = progress;
            if (onChunk) onChunk(chunk);
        };
        return msb::transcribe(*model_, audio.data(), audio.size(), programs, &all, cancel, onUpdate, reason);
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
    const ModelSize size = modelSize();
    const std::string path = muscriptorModelPath();
    const bool isOverride = std::getenv(kMuScriptorModelEnv) != nullptr;
    if (!fileExists(path)) {
        if (reason) *reason = isOverride ? "model not found: " + path : pendingReason(size);
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

const ModelFile& modelFile(ModelSize size) { return kModelFiles[std::clamp(int(size), 0, kNumModelSizes - 1)]; }

std::string muscriptorModelPath(ModelSize size) { return libraryFolder() + "/models/" + modelFile(size).name; }

std::string muscriptorModelPath() {
    if (const char* env = std::getenv(kMuScriptorModelEnv); env && *env) return env;
    return muscriptorModelPath(modelSize());
}

void installMuScriptorLoader() {
    static std::once_flag once;
    std::call_once(once, [] {
        ModelSize saved = kDefaultModelSize;
        if (readChoice(&saved)) setModelSize(saved);  // before the store hook: restoring is not a new choice
        setModelSizeStore(storeChoice);
        setModelAvailability([](ModelSize size) { return fileExists(muscriptorModelPath(size)); });
        setTranscriberLoader([](std::string* reason) { return loadTranscriber(reason); });
        setTranscriberPrefetch([]() -> std::string {
            if (!msb::isSupported()) return "Tonetrace needs an Apple Silicon Mac for the transcription";
            if (std::getenv(kMuScriptorModelEnv) || fileExists(muscriptorModelPath())) return {};
            const ModelSize size = modelSize();
            startDownload(size);
            return std::string("Downloading the ") + modelSizeInfo(size).label + " transcription model (" + modelSizeInfo(size).downloadText +
                   ") once - songs are transcribed as soon as it is ready";
        });
    });
}

}  // namespace ttr
