// MuScriptor (multi-instrument transcription, drums included): the interface the core knows, the model registry and
// the conversion of model notes into Tonetrace notes. The implementation (download, muscriptor.cpp) lives in
// MuScriptorTranscriber.mm (tonetrace_ml); tests register a fake.
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
    int program = 0;                   // GM program the model decoded (kDrumProgram for a drum hit)
};

// What one chunk (5 s of audio) added. Each note is reported once and never changes.
struct TranscriptionChunk {
    std::vector<TranscribedNote> newNotes;
    double finalizedSec = 0.0;  // every note ending before this is reported
    float progress = 0.0f;      // 0..1
};
using ChunkCallback = std::function<void(const TranscriptionChunk& chunk)>;

class NoteTranscriber {
public:
    virtual ~NoteTranscriber() = default;
    // 16 kHz mono in; `onChunk` after every chunk. `programs` restricts the model to these instruments (one program per
    // instrument group, kDrumProgram for drums; empty = every instrument). False with a reason on failure or cancel.
    virtual bool transcribe(const std::vector<float>& audio16k, const std::vector<int>& programs, const ChunkCallback& onChunk, TaskControl* control,
                            std::string* reason) = 0;
};

// ---- model size (one choice per machine; the loader reads it) ----
enum class ModelSize : int { Small = 0, Medium = 1, Large = 2 };
constexpr int kNumModelSizes = 3;
constexpr ModelSize kDefaultModelSize = ModelSize::Medium;

struct ModelSizeInfo {
    const char* id;            // "small" (file names, settings)
    const char* label;         // "Small"
    const char* downloadText;  // "209 MB"
    const char* character;     // "fastest, rougher parts"
};
const ModelSizeInfo& modelSizeInfo(ModelSize size);
bool parseModelSize(const std::string& id, ModelSize* out);
ModelSize modelSize();
// Chooses the model for the next transcription: a loaded model of another size is released and the store hook (if any)
// remembers the choice.
void setModelSize(ModelSize size);
// Called with every new choice (the plug-in writes it to its settings file).
void setModelSizeStore(std::function<void(ModelSize)> store);
// Whether a size is on this Mac already (no download needed). False without a hook.
void setModelAvailability(std::function<bool(ModelSize)> isAvailable);
bool isModelAvailable(ModelSize size);

// Returns the transcriber for modelSize(), or null with a reason (e.g. "model downloading (35 %)"). Asked again on
// every song until it succeeds, because the model may still be downloading.
using TranscriberLoader = std::function<std::shared_ptr<NoteTranscriber>(std::string* reason)>;

void setTranscriberLoader(TranscriberLoader loader);
void setTranscriber(std::shared_ptr<NoteTranscriber> transcriber);
std::shared_ptr<NoteTranscriber> noteTranscriber(std::string* reason = nullptr);
// "not loaded yet", "ready" or the last reason it was unavailable (diagnostics, status bar).
std::string transcriberStatus();
// Non-blocking: lets the implementation start fetching the model early. Returns a hint for the user when something
// happens in the background (e.g. "downloading the model"), empty otherwise.
void setTranscriberPrefetch(std::function<std::string()> prefetch);
std::string prefetchTranscriber();

// Model notes as Tonetrace notes: outside the piano roll dropped, drum hits (10 ms in the model) lengthened to a 1/16
// note at `bpm` but never into the next hit of the same drum, every note with its instrument. Sorted by start.
std::vector<NoteEvent> muscriptorNotes(const std::vector<TranscribedNote>& notes, double bpm);

// Streaming update of a whole-song transcription: all notes so far (converted), how far they are final, progress.
using SongProgress = std::function<void(const std::vector<NoteEvent>& notes, double finalizedSec, float progress)>;
// Runs the model on 16 kHz mono audio (restricted to `programs`, empty = every instrument), calling `onProgress` after
// every chunk. False with a reason when the model is missing, fails or the task is cancelled ("cancelled").
bool transcribeSong(const std::vector<float>& audio16k, double bpm, const std::vector<int>& programs, TaskControl* control, const SongProgress& onProgress,
                    std::string* reason);

}  // namespace ttr
