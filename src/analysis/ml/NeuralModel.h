// Neural transcription model (Basic Pitch layout): interface, process-wide registry and the windowing of a whole recording.
// The core only knows this interface; the CoreML implementation lives in NeuralTranscriber.mm (tonetrace_ml).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "analysis/TaskControl.h"

namespace ttr {

constexpr double kNeuralRate = 22050.0;
constexpr int kNeuralHop = 256;                                   // samples per output frame (86.13 frames/s)
constexpr int kNeuralWindowSamples = 2 * 22050 - kNeuralHop;      // 43844: model input length
constexpr int kNeuralWindowFrames = 172;                          // model output frames per window
constexpr int kNeuralOverlapFrames = 30;                          // overlap between windows, half is cut on each side
constexpr int kNeuralKeys = 88;                                   // A0 (MIDI 21) .. C8
constexpr int kNeuralLowestMidi = 21;
constexpr int kNeuralContourBins = 3 * kNeuralKeys;               // 3 bins per semitone
constexpr double kNeuralFps = kNeuralRate / kNeuralHop;

// Frame-major matrices: value(frame, key) = note[frame * kNeuralKeys + key].
struct Activations {
    int frames = 0;
    std::vector<float> note, onset, contour;
    float noteAt(int frame, int key) const { return note[size_t(frame) * kNeuralKeys + size_t(key)]; }
    float onsetAt(int frame, int key) const { return onset[size_t(frame) * kNeuralKeys + size_t(key)]; }
    float contourAt(int frame, int bin) const { return contour[size_t(frame) * kNeuralContourBins + size_t(bin)]; }
};

class NeuralModel {
public:
    virtual ~NeuralModel() = default;
    // One window of kNeuralWindowSamples mono samples at kNeuralRate in, kNeuralWindowFrames frames of each output out
    // (note / onset: frames x 88, contour: frames x 264, frame-major). False on any failure.
    virtual bool infer(const float* window, float* note, float* onset, float* contour) = 0;
};

// Creates the model on first use (on a worker thread, never the audio thread). Returns null and a reason on failure.
using NeuralLoader = std::function<std::shared_ptr<NeuralModel>(std::string* reason)>;

// Registry (thread-safe). The loader runs at most once; a test can install a model directly.
void setNeuralLoader(NeuralLoader loader);
void setNeuralModel(std::shared_ptr<NeuralModel> model);
std::shared_ptr<NeuralModel> neuralModel(std::string* reason = nullptr);
// "not loaded yet", "ready" or the reason it failed (for the diagnostics report).
std::string neuralStatus();

// Runs the model over the whole signal (mono, kNeuralRate) in overlapping windows and joins the outputs.
// False when the model fails or the task is cancelled.
bool transcribe(NeuralModel& model, const std::vector<float>& audio, Activations* out, TaskControl* control = nullptr);

}  // namespace ttr
