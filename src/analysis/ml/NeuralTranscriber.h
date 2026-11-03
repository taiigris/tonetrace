// CoreML implementation of NeuralModel (Basic Pitch, compiled to nmp.mlmodelc). Part of tonetrace_ml (Objective-C++).
#pragma once
#include <memory>
#include <string>

#include "analysis/ml/NeuralModel.h"

namespace ttr {

constexpr const char* kNeuralModelName = "nmp.mlmodelc";
constexpr const char* kNeuralModelEnv = "TONETRACE_NEURAL_MODEL";  // overrides the search (tools, tests)

// Loads a compiled model directory; null and a reason on failure.
std::shared_ptr<NeuralModel> loadCoreMLModel(const std::string& path, std::string* reason);
// Installs the loader that finds the model: $TONETRACE_NEURAL_MODEL, then Contents/Resources of the bundle that holds
// this code, then the directory of the executable. Loading happens on the first Neural Quick Grab. Only the first call counts.
void installCoreMLLoader();

}  // namespace ttr
