// MuScriptor for the plugin: model file management (three sizes, each downloaded from Hugging Face on first use and
// SHA256-checked), the saved model choice and the NoteTranscriber over muscriptor.cpp. The weights are CC BY-NC 4.0
// (non-commercial) and are never bundled.
#pragma once
#include <string>

#include "analysis/ml/MuScriptorEngine.h"

namespace ttr {

constexpr const char* kMuScriptorModelEnv = "TONETRACE_MUSCRIPTOR_MODEL";  // explicit model path (tests, tools); ignores the size

struct ModelFile {
    const char* name;  // also the file name under the Hugging Face folder
    const char* sha256;
    long long bytes;
};
const ModelFile& modelFile(ModelSize size);

// The model of the chosen size (~/Library/Tonetrace/models/<name>), or the env override.
std::string muscriptorModelPath();
// The file of one size, never the override.
std::string muscriptorModelPath(ModelSize size);
// Registers the loader, the prefetch hook and the model-size hooks with the core registry and restores the saved model
// choice (~/Library/Tonetrace/model-size). Idempotent.
void installMuScriptorLoader();

}  // namespace ttr
