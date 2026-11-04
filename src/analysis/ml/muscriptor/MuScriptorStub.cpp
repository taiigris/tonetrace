// x86_64 slice of the MuScriptor bridge: muscriptor.cpp is only built for Apple Silicon.
#include "analysis/ml/muscriptor/MuScriptorBridge.h"

namespace ttr::msb {

class Model {};

bool isSupported() { return false; }

std::shared_ptr<Model> load(const std::string&, std::string* reason) {
    if (reason) *reason = "MuScriptor needs an Apple Silicon Mac";
    return nullptr;
}

bool transcribe(Model&, const float*, size_t, std::vector<Note>*, const std::function<bool()>&, const UpdateCallback&, std::string* reason) {
    if (reason) *reason = "MuScriptor needs an Apple Silicon Mac";
    return false;
}

std::string deviceName(const Model&) { return {}; }

}  // namespace ttr::msb
