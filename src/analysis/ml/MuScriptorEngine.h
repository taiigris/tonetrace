// Quick Grab with MuScriptor (multi-instrument transcription, drums included): interface, registry and the note conversion.
// The core only knows this interface; the implementation (download, muscriptor.cpp) lives in MuScriptorTranscriber.mm (tonetrace_ml).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "analysis/TaskControl.h"
#include "core/Types.h"

namespace ttr {

constexpr double kMuScriptorRate = 16000.0;  // model input rate

struct TranscribedNote {
    double onset = 0.0, offset = 0.0;  // seconds
    int pitch = 60;                    // MIDI; GM percussion note when isDrum
    bool isDrum = false;
};

class NoteTranscriber {
public:
    virtual ~NoteTranscriber() = default;
    // 16 kHz mono in, every note out. False with a reason on failure or cancel.
    virtual bool transcribe(const std::vector<float>& audio, std::vector<TranscribedNote>* out, TaskControl* control, std::string* reason) = 0;
};

// Returns the transcriber, or null with a reason (e.g. "model downloading (35 %)"). Unlike the Basic Pitch loader it is asked
// again on every Quick Grab until it succeeds, because the model may still be downloading.
using TranscriberLoader = std::function<std::shared_ptr<NoteTranscriber>(std::string* reason)>;

void setTranscriberLoader(TranscriberLoader loader);
void setTranscriber(std::shared_ptr<NoteTranscriber> transcriber);
std::shared_ptr<NoteTranscriber> noteTranscriber(std::string* reason = nullptr);
// "not loaded yet", "ready" or the last reason it was unavailable (for the diagnostics report).
std::string transcriberStatus();
// Non-blocking: lets the implementation start fetching the model early (when the user picks the engine). Returns a hint for
// the user when something is happening in the background (e.g. "downloading the model"), empty otherwise.
void setTranscriberPrefetch(std::function<std::string()> prefetch);
std::string prefetchTranscriber();

// Model notes as Tonetrace notes: outside the note map dropped, drum hits (10 ms in the model) lengthened to a 1/16 note at
// `bpm`, but never into the next hit of the same drum.
std::vector<NoteEvent> muscriptorNotes(const std::vector<TranscribedNote>& notes, double bpm);

struct MuScriptorGrab {
    bool ok = false;
    std::string reason;
    std::vector<NoteEvent> notes;
};
// The whole path for a document: stored recording -> 16 kHz -> transcriber -> notes. ok = false when anything is missing.
MuScriptorGrab muscriptorGrabSnapshot(const Snapshot& snapshot, TaskControl* control);

}  // namespace ttr
