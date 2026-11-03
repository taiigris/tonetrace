// MuScriptor for the plugin: model file management (download from Hugging Face on first use, SHA256-checked) and the
// NoteTranscriber over muscriptor.cpp. The weights are CC BY-NC 4.0 (non-commercial) and are never bundled.
#pragma once
#include <string>

namespace ttr {

constexpr const char* kMuScriptorModelEnv = "TONETRACE_MUSCRIPTOR_MODEL";  // explicit model path (tests, tools)
constexpr const char* kMuScriptorModelName = "muscriptor-medium-f16.gguf";
constexpr const char* kMuScriptorModelUrl = "https://huggingface.co/DamRsn/muscriptor-gguf/resolve/main/v1/muscriptor-medium-f16.gguf";
constexpr const char* kMuScriptorModelSha256 = "3850cc9e5b436b17a09bd25b8f2615cb3366ab96a71e7b50f73a793a917fdf03";
constexpr long long kMuScriptorModelBytes = 618442496;

// ~/Library/Tonetrace/models/<name>, or the env override.
std::string muscriptorModelPath();
// Registers the loader and the prefetch hook with the core registry (idempotent).
void installMuScriptorLoader();

}  // namespace ttr
