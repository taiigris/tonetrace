#include "analysis/ml/MuScriptorEngine.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace ttr {

namespace {

constexpr double kSecondsPerMinute = 60.0;
constexpr double kFallbackBpm = 120.0;
constexpr double kDrumBeatShare = 0.25;  // drum hits last a 1/16 note
constexpr double kMinNoteSec = 0.01;     // the model's shortest note

struct Registry {
    std::mutex mutex;      // guards the fields
    std::mutex loadMutex;  // serialises loader calls (they can take seconds)
    TranscriberLoader loader;
    std::function<std::string()> prefetch;
    std::function<void(ModelSize)> storeSize;
    std::function<bool(ModelSize)> isAvailable;
    std::shared_ptr<NoteTranscriber> transcriber;
    ModelSize size = kDefaultModelSize;
    std::string status = "not loaded yet";
};

constexpr ModelSizeInfo kModelSizes[kNumModelSizes] = {
    {"small", "Small", "209 MB", "fastest, rougher parts"},
    {"medium", "Medium", "618 MB", "balanced (recommended)"},
    {"large", "Large", "2.7 GB", "most accurate, slowest"},
};

Registry& registry() {
    static Registry r;
    return r;
}

double drumLength(double bpm) {
    const double usable = bpm > 0.0 && std::isfinite(bpm) ? bpm : kFallbackBpm;
    return kSecondsPerMinute / usable * kDrumBeatShare;
}

bool usable(const TranscribedNote& t) {
    return t.pitch >= kMinNote && t.pitch <= kMaxNote && std::isfinite(t.onset) && std::isfinite(t.offset);
}

}  // namespace

const ModelSizeInfo& modelSizeInfo(ModelSize size) {
    const int index = std::clamp(int(size), 0, kNumModelSizes - 1);
    return kModelSizes[index];
}

bool parseModelSize(const std::string& id, ModelSize* out) {
    for (int i = 0; i < kNumModelSizes; ++i)
        if (id == kModelSizes[i].id) {
            *out = ModelSize(i);
            return true;
        }
    return false;
}

ModelSize modelSize() {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    return r.size;
}

void setModelSize(ModelSize size) {
    Registry& r = registry();
    std::function<void(ModelSize)> store;
    {
        std::lock_guard<std::mutex> guard(r.mutex);
        if (r.size == size) return;
        r.size = size;
        r.transcriber.reset();  // a running transcription keeps its own reference
        r.status = "not loaded yet";
        store = r.storeSize;
    }
    if (store) store(size);
}

void setModelSizeStore(std::function<void(ModelSize)> store) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.storeSize = std::move(store);
}

void setModelAvailability(std::function<bool(ModelSize)> isAvailable) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.isAvailable = std::move(isAvailable);
}

bool isModelAvailable(ModelSize size) {
    std::function<bool(ModelSize)> isAvailable;
    {
        Registry& r = registry();
        std::lock_guard<std::mutex> guard(r.mutex);
        isAvailable = r.isAvailable;
    }
    return isAvailable && isAvailable(size);
}

void setTranscriberLoader(TranscriberLoader loader) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.loader = std::move(loader);
    r.transcriber.reset();
    r.status = "not loaded yet";
}

void setTranscriber(std::shared_ptr<NoteTranscriber> transcriber) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.transcriber = std::move(transcriber);
    r.status = r.transcriber ? "ready" : "no model";
}

std::shared_ptr<NoteTranscriber> noteTranscriber(std::string* reason) {
    Registry& r = registry();
    std::lock_guard<std::mutex> loading(r.loadMutex);  // one load at a time; the status stays readable meanwhile
    TranscriberLoader loader;
    ModelSize size;
    {
        std::lock_guard<std::mutex> guard(r.mutex);
        if (r.transcriber) return r.transcriber;
        loader = r.loader;
        size = r.size;
    }
    std::string why = "no MuScriptor engine installed";
    std::shared_ptr<NoteTranscriber> loaded = loader ? loader(&why) : nullptr;
    std::lock_guard<std::mutex> guard(r.mutex);
    if (r.size != size) {  // the choice changed while loading: the caller may use it once, it is not kept
        if (!loaded && reason) *reason = why;
        return loaded;
    }
    if (!r.transcriber) r.transcriber = loaded;
    r.status = r.transcriber ? "ready" : why;
    if (!r.transcriber && reason) *reason = r.status;
    return r.transcriber;
}

std::string transcriberStatus() {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    return r.status;
}

void setTranscriberPrefetch(std::function<std::string()> prefetch) {
    Registry& r = registry();
    std::lock_guard<std::mutex> guard(r.mutex);
    r.prefetch = std::move(prefetch);
}

std::string prefetchTranscriber() {
    std::function<std::string()> prefetch;
    {
        Registry& r = registry();
        std::lock_guard<std::mutex> guard(r.mutex);
        if (r.transcriber) return {};
        prefetch = r.prefetch;
    }
    return prefetch ? prefetch() : std::string();
}

std::vector<NoteEvent> muscriptorNotes(const std::vector<TranscribedNote>& notes, double bpm) {
    const double drum = drumLength(bpm);
    // Next hit of every drum: the hits per drum sorted by onset, then a binary search (O(n log n) for the whole song).
    std::map<int, std::vector<double>> hits;
    for (const TranscribedNote& t : notes)
        if (t.isDrum && usable(t)) hits[t.pitch].push_back(t.onset);
    for (auto& entry : hits) std::sort(entry.second.begin(), entry.second.end());
    auto nextHit = [&hits](const TranscribedNote& t) {
        const std::vector<double>& times = hits[t.pitch];
        const auto it = std::upper_bound(times.begin(), times.end(), t.onset);
        return it == times.end() ? INFINITY : *it;
    };
    std::vector<NoteEvent> out;
    out.reserve(notes.size());
    for (const TranscribedNote& t : notes) {
        if (!usable(t)) continue;
        NoteEvent n;
        n.pitch = t.pitch;
        n.startSec = std::max(0.0, t.onset);
        const double end = t.isDrum ? std::min(n.startSec + drum, nextHit(t)) : t.offset;
        n.endSec = std::max(end, n.startSec + kMinNoteSec);
        n.program = t.isDrum ? kDrumProgram : (t.program >= 0 && t.program < kDrumProgram ? t.program : kNoProgram);
        out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](const NoteEvent& a, const NoteEvent& b) {
        if (a.startSec != b.startSec) return a.startSec < b.startSec;
        return a.pitch != b.pitch ? a.pitch < b.pitch : a.program < b.program;
    });
    return out;
}

bool transcribeSong(const std::vector<float>& audio16k, double bpm, const std::vector<int>& programs, TaskControl* control, const SongProgress& onProgress,
                    std::string* reason) {
    std::string why;
    if (audio16k.empty()) why = "no audio";
    const std::shared_ptr<NoteTranscriber> transcriber = why.empty() ? noteTranscriber(&why) : nullptr;
    if (!transcriber) {
        if (reason) *reason = why;
        return false;
    }
    std::vector<TranscribedNote> raw;
    const ChunkCallback onChunk = [&](const TranscriptionChunk& chunk) {
        raw.insert(raw.end(), chunk.newNotes.begin(), chunk.newNotes.end());
        if (control) control->report(chunk.progress);
        if (onProgress) onProgress(muscriptorNotes(raw, bpm), chunk.finalizedSec, chunk.progress);
    };
    if (!transcriber->transcribe(audio16k, programs, onChunk, control, &why)) {
        if (reason) *reason = control && control->cancelled() ? "cancelled" : why;
        return false;
    }
    if (control) control->report(1.0f);
    return true;
}

}  // namespace ttr
