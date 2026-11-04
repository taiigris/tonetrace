// muscriptor.cpp behind the C++17 bridge (compiled as C++23 in the arm64 sub-build, cmake/muscriptor).
#include "analysis/ml/muscriptor/MuScriptorBridge.h"

#include <muscriptor/muscriptor.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <optional>

namespace ttr::msb {

class Model {
public:
    explicit Model(msl::Transcriber t) : transcriber(std::move(t)) {}
    std::mutex mutex;
    std::optional<msl::Transcriber> transcriber;  // emptied at exit, see releaseAtExit
};

namespace {

// ggml frees its Metal devices from a static destructor and asserts that no model still holds GPU buffers. Our models live
// in a static registry that is destroyed later, so the weights are released by an exit handler registered after the first
// load (exit handlers run in reverse order of registration, so it runs before ggml's teardown; also on plugin unload).
std::mutex liveMutex;
std::vector<std::weak_ptr<Model>>& liveModels() {
    static auto* models = new std::vector<std::weak_ptr<Model>>();  // never destroyed: used by the exit handler
    return *models;
}

void releaseAll() {
    std::lock_guard<std::mutex> guard(liveMutex);
    for (const std::weak_ptr<Model>& weak : liveModels())
        if (std::shared_ptr<Model> model = weak.lock()) {
            std::lock_guard<std::mutex> lock(model->mutex);
            model->transcriber.reset();
        }
    liveModels().clear();
}

void track(const std::shared_ptr<Model>& model) {
    static const bool isRegistered = std::atexit(releaseAll) == 0;
    (void)isRegistered;
    std::lock_guard<std::mutex> guard(liveMutex);
    liveModels().push_back(model);
}

}  // namespace

bool isSupported() { return true; }

std::shared_ptr<Model> load(const std::string& path, std::string* reason) {
    try {
        auto loaded = msl::Transcriber::load(path);
        if (!loaded) {
            if (reason) *reason = std::string("cannot load the model: ") + msl::describe(loaded.error());
            return nullptr;
        }
        auto model = std::make_shared<Model>(std::move(*loaded));
        track(model);
        return model;
    } catch (const std::exception& e) {
        if (reason) *reason = std::string("cannot load the model: ") + e.what();
        return nullptr;
    }
}

bool transcribe(Model& model, const float* samples, size_t count, std::vector<Note>* out, const std::function<bool()>& cancel,
                const std::function<void(float)>& progress, std::string* reason) {
    std::lock_guard<std::mutex> lock(model.mutex);  // one call at a time per instance (KV cache)
    if (!model.transcriber) {
        if (reason) *reason = "the model was released";
        return false;
    }
    msl::TranscribeOptions options;  // no instrument selection: every instrument, drums included
    if (cancel) options.should_cancel = cancel;
    const msl::NoteCallback onChunk = [&](const msl::TranscriptionUpdate& update) {
        if (progress) progress(std::clamp(float(update.progress), 0.0f, 1.0f));
        return !(cancel && cancel());
    };
    try {
        auto notes = model.transcriber->transcribe(std::span<const float>(samples, count), options, onChunk);
        if (!notes) {
            if (reason) *reason = notes.error() == msl::Error::Cancelled ? "cancelled" : std::string("the model failed: ") + msl::describe(notes.error());
            return false;
        }
        out->clear();
        out->reserve(notes->size());
        for (const msl::Note& n : *notes) out->push_back({n.onset, n.offset, n.pitch, n.is_drum, n.program});
        return true;
    } catch (const std::exception& e) {
        if (reason) *reason = std::string("the model failed: ") + e.what();
        return false;
    }
}

std::string deviceName(const Model& model) { return model.transcriber ? model.transcriber->device().name : std::string(); }

}  // namespace ttr::msb
