// C++17 face of muscriptor.cpp (C++23). MuScriptorBridge.cpp implements it on arm64, MuScriptorStub.cpp on x86_64
// (muscriptor.cpp builds one architecture per tree; Intel Macs get "not supported").
#pragma once
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ttr::msb {

constexpr double kSampleRate = 16000.0;  // the model's input rate

struct Note {
    double onset = 0.0, offset = 0.0;  // seconds in the input
    int pitch = 0;                     // MIDI; GM percussion note for drums
    bool isDrum = false;
};

class Model;  // opaque, owns the loaded weights

// False on a build without the engine (x86_64).
bool isSupported();
// Loads a GGUF checkpoint (GPU when available, else CPU). Null and a reason on failure.
std::shared_ptr<Model> load(const std::string& path, std::string* reason);
// Transcribes 16 kHz mono audio. `cancel` is polled often, `progress` gets 0..1. False with a reason on failure or cancel.
bool transcribe(Model& model, const float* samples, size_t count, std::vector<Note>* out, const std::function<bool()>& cancel,
                const std::function<void(float)>& progress, std::string* reason);
// Name of the device the model runs on ("Apple M1 Pro", "CPU").
std::string deviceName(const Model& model);

}  // namespace ttr::msb
