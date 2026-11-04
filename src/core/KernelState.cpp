// TonetraceKernel: the host state. A song keeps its notes and its file path (the audio is read again from the file),
// a recording keeps its audio (mono, 22.05 kHz).
#include <algorithm>
#include <cmath>

#include "analysis/ml/Resampler.h"
#include "core/Instruments.h"
#include "core/Logger.h"
#include "core/SongAudio.h"
#include "core/TonetraceKernel.h"
#include "state/StateCodec.h"

namespace ttr {

namespace {

constexpr double kStoredRecordingRate = 22050.0;

// The recording as it goes into the state: mono at 22.05 kHz.
std::shared_ptr<const SongAudio> storedRecording(const SongAudio& audio) {
    const std::vector<float> mono = monoOf(audio);
    const std::vector<float> reduced = resampleMono(mono.data(), mono.size(), audio.sampleRate, kStoredRecordingRate);
    return makeSongAudio(reduced.data(), reduced.size(), 1, kStoredRecordingRate);
}

}  // namespace

std::vector<uint8_t> TonetraceKernel::saveState() const {
    const DocumentPtr doc = document();
    const double user = exportBpm_.load();
    const StateChoices choices{instrumentSelection(), modelFocus()};
    if (!doc || !doc->isRecording || !doc->audio) return encodeState(allParams(), doc.get(), mixer(), user, choices);
    Document stored = *doc;
    stored.audio = storedRecording(*doc->audio);
    return encodeState(allParams(), &stored, mixer(), user, choices);
}

bool TonetraceKernel::loadState(const uint8_t* data, size_t size) {
    DecodedState state;
    std::string reason;
    if (!decodeState(data, size, &state, &reason)) {
        logError(instanceId_, "state load failed (%zu bytes): %s", size, reason.c_str());
        diagnostics_.setLastError("state load failed: " + reason);
        notice("The saved state could not be loaded: " + reason, NoticeLevel::Error);
        return false;
    }
    stopJob();
    for (int i = 0; i < kNumParams; ++i) setParam(i, state.params[size_t(i)]);
    exportBpm_.store(state.exportBpm);
    {
        std::lock_guard<std::mutex> guard(docMutex_);
        mixer_ = state.mixer;
        instrumentSelection_ = normalizedSelection(state.instrumentSelection);
        isModelFocused_ = state.isModelFocused;
    }
    pause();
    seek(0.0);
    std::shared_ptr<Document> doc;
    if (state.document) {
        doc = std::make_shared<Document>(*state.document);
        doc->id = nextDocumentId();
        doc->version = 1;
        doc->transcription.listenedFor = listenFor();  // analysed with the choice that was saved with it
        {
            std::lock_guard<std::mutex> guard(docMutex_);
            applySelection(*doc);
        }
        if (doc->audio) {
            doc->peaks = computePeaks(*doc->audio);
            doc->lengthSec = std::max(doc->lengthSec, doc->audio->seconds());
        }
    }
    publish(doc);
    if (doc && !doc->isRecording && !doc->sourcePath.empty()) {
        const uint64_t id = doc->id;
        const std::string path = doc->sourcePath;
        startJob([this, id, path](TaskControl& control) { runReloadAudio(control, id, path); });
    }
    logInfo(instanceId_, "state loaded: %zu bytes, format v%u, %zu notes, %s", size, state.formatVersion, doc ? doc->notes.size() : size_t(0),
            doc ? doc->sourceName.c_str() : "no document");
    return true;
}

}  // namespace ttr
