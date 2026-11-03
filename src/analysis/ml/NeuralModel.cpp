#include "analysis/ml/NeuralModel.h"

#include <algorithm>
#include <cmath>
#include <mutex>

namespace ttr {

namespace {

struct Registry {
    std::mutex mutex;
    NeuralLoader loader;
    std::shared_ptr<NeuralModel> model;
    bool isTried = false;
    std::string status = "not loaded yet";
};

Registry& registry() {
    static Registry r;
    return r;
}

constexpr int kCutFrames = kNeuralOverlapFrames / 2;
constexpr int kKeptFrames = kNeuralWindowFrames - kNeuralOverlapFrames;               // 142 per window
constexpr int kWindowHop = kNeuralWindowSamples - kNeuralOverlapFrames * kNeuralHop;  // 36164 samples

}  // namespace

void setNeuralLoader(NeuralLoader loader) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.loader = std::move(loader);
    r.model.reset();
    r.isTried = false;
    r.status = "not loaded yet";
}

void setNeuralModel(std::shared_ptr<NeuralModel> model) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.model = std::move(model);
    r.isTried = true;
    r.status = r.model ? "ready" : "no model";
}

std::shared_ptr<NeuralModel> neuralModel(std::string* reason) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    if (!r.isTried) {
        r.isTried = true;
        std::string why = "no model installed";
        if (r.loader) r.model = r.loader(&why);
        r.status = r.model ? "ready" : why;
    }
    if (!r.model && reason) *reason = r.status;
    return r.model;
}

std::string neuralStatus() {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    return r.status;
}

// Same windows as Basic Pitch's own inference: half an overlap of silence in front, windows every kWindowHop samples,
// the first and last kCutFrames of every window dropped. A window advances 141.27 frames but keeps 142, so every kept frame
// is placed by its own sample position (plain concatenation would drift by 0.3 s per minute); the last frame of a window
// is overwritten by the next window where they meet.
bool transcribe(NeuralModel& model, const std::vector<float>& audio, Activations* out, TaskControl* control) {
    *out = Activations{};
    if (audio.empty()) return true;
    const size_t pad = size_t(kNeuralOverlapFrames * kNeuralHop / 2);
    const size_t padded = audio.size() + pad;
    const size_t windows = (padded + size_t(kWindowHop) - 1) / size_t(kWindowHop);
    out->frames = int((audio.size() + size_t(kNeuralHop) - 1) / size_t(kNeuralHop));
    out->note.assign(size_t(out->frames) * kNeuralKeys, 0.0f);
    out->onset.assign(size_t(out->frames) * kNeuralKeys, 0.0f);
    out->contour.assign(size_t(out->frames) * kNeuralContourBins, 0.0f);

    std::vector<float> window(static_cast<size_t>(kNeuralWindowSamples));
    std::vector<float> note(size_t(kNeuralWindowFrames) * kNeuralKeys), onset(note.size()), contour(size_t(kNeuralWindowFrames) * kNeuralContourBins);
    for (size_t w = 0; w < windows; ++w) {
        if (control && control->cancelled()) return false;
        const size_t start = w * size_t(kWindowHop);  // in padded samples
        for (size_t i = 0; i < window.size(); ++i) {
            const size_t p = start + i;
            window[i] = p >= pad && p - pad < audio.size() ? audio[p - pad] : 0.0f;
        }
        if (!model.infer(window.data(), note.data(), onset.data(), contour.data())) return false;
        const int firstFrame = int(std::lround(double(start) / kNeuralHop));  // frame of padded sample start + kCutFrames * hop
        for (int f = 0; f < kKeptFrames; ++f) {
            const int dst = firstFrame + f;
            if (dst >= out->frames) break;
            const size_t src = size_t(f + kCutFrames);
            std::copy_n(&note[src * kNeuralKeys], kNeuralKeys, &out->note[size_t(dst) * kNeuralKeys]);
            std::copy_n(&onset[src * kNeuralKeys], kNeuralKeys, &out->onset[size_t(dst) * kNeuralKeys]);
            std::copy_n(&contour[src * kNeuralContourBins], kNeuralContourBins, &out->contour[size_t(dst) * kNeuralContourBins]);
        }
        if (control) control->report(0.8f * float(w + 1) / float(windows));
    }
    return true;
}

}  // namespace ttr
