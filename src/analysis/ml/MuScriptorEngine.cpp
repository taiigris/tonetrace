#include "analysis/ml/MuScriptorEngine.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "analysis/ml/Resampler.h"

namespace ttr {

namespace {

constexpr double kSecondsPerMinute = 60.0;
constexpr double kFallbackBpm = 120.0;
constexpr double kDrumBeatShare = 0.25;    // drum hits last a 1/16 note
constexpr double kMinNoteSec = 0.01;       // the model's shortest note

struct Registry {
    std::mutex mutex;      // guards the fields
    std::mutex loadMutex;  // serialises loader calls (they can take seconds)
    TranscriberLoader loader;
    std::function<std::string()> prefetch;
    std::shared_ptr<NoteTranscriber> transcriber;
    std::string status = "not loaded yet";
};

Registry& registry() {
    static Registry r;
    return r;
}

double drumLength(double bpm) {
    const double usable = bpm > 0.0 && std::isfinite(bpm) ? bpm : kFallbackBpm;
    return kSecondsPerMinute / usable * kDrumBeatShare;
}

// Start of the next hit of the same drum after `index` (or infinity).
double nextHitOf(const std::vector<TranscribedNote>& notes, size_t index) {
    const TranscribedNote& self = notes[index];
    double next = INFINITY;
    for (size_t k = 0; k < notes.size(); ++k)
        if (k != index && notes[k].isDrum && notes[k].pitch == self.pitch && notes[k].onset > self.onset) next = std::min(next, notes[k].onset);
    return next;
}

}  // namespace

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
    {
        std::lock_guard<std::mutex> guard(r.mutex);
        if (r.transcriber) return r.transcriber;
        loader = r.loader;
    }
    std::string why = "no MuScriptor engine installed";
    std::shared_ptr<NoteTranscriber> loaded = loader ? loader(&why) : nullptr;
    std::lock_guard<std::mutex> guard(r.mutex);
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
    std::vector<NoteEvent> out;
    out.reserve(notes.size());
    for (size_t i = 0; i < notes.size(); ++i) {
        const TranscribedNote& t = notes[i];
        if (t.pitch < kMinNote || t.pitch > kMaxNote || !std::isfinite(t.onset) || !std::isfinite(t.offset)) continue;
        NoteEvent n;
        n.pitch = t.pitch;
        n.startSec = std::max(0.0, t.onset);
        const double end = t.isDrum ? std::min(n.startSec + drum, nextHitOf(notes, i)) : t.offset;
        n.endSec = std::max(end, n.startSec + kMinNoteSec);
        n.strength = 1.0f;
        n.selected = false;
        n.program = t.isDrum ? kDrumProgram : (t.program >= 0 && t.program < kDrumProgram ? t.program : kNoProgram);
        out.push_back(n);
    }
    std::sort(out.begin(), out.end(), [](const NoteEvent& a, const NoteEvent& b) { return a.startSec != b.startSec ? a.startSec < b.startSec : a.pitch < b.pitch; });
    return out;
}

namespace {

// Runs the model on 16 kHz audio (built only once the model is there, so a missing model costs no resampling).
MuScriptorGrab transcribe16k(const std::function<std::vector<float>()>& audio16k, double bpm, TaskControl* control) {
    MuScriptorGrab result;
    const std::shared_ptr<NoteTranscriber> transcriber = noteTranscriber(&result.reason);
    if (!transcriber) return result;
    std::vector<TranscribedNote> raw;
    if (!transcriber->transcribe(audio16k(), &raw, control, &result.reason)) {
        if (control && control->cancelled()) result.reason = "cancelled";
        return result;
    }
    result.notes = muscriptorNotes(raw, bpm);
    result.ok = true;
    if (control) control->report(1.0f);
    return result;
}

}  // namespace

MuScriptorGrab muscriptorGrabSnapshot(const Snapshot& snapshot, TaskControl* control) {
    if (!snapshot.audio || snapshot.audio->samples.empty()) {
        MuScriptorGrab result;
        result.reason = "the recording audio is not stored (longer than 60 s)";
        return result;
    }
    const AudioClip& clip = *snapshot.audio;
    return transcribe16k([&clip] { return resampleInt16(clip.samples.data(), clip.samples.size(), clip.sampleRate, kMuScriptorRate); },
                         snapshot.bpm, control);
}

MuScriptorGrab muscriptorTranscribe(const std::vector<float>& mono, double sampleRate, double bpm, TaskControl* control) {
    if (mono.empty() || !(sampleRate > 0.0)) {
        MuScriptorGrab result;
        result.reason = "no audio";
        return result;
    }
    return transcribe16k([&] { return resampleMono(mono.data(), mono.size(), sampleRate, kMuScriptorRate); }, bpm, control);
}

}  // namespace ttr
