// Pitch-class statistics and Krumhansl-Schmuckler key estimation.
#pragma once
#include <array>
#include <string>
#include <vector>

#include "core/Types.h"

namespace ttr {

std::array<float, 12> pitchClassWeights(const std::vector<NoteEvent>& notes);  // sum(strength * duration), max-normalised
KeyResult estimateKey(const std::array<float, 12>& weights);
std::string keyName(const KeyResult& key);          // "A minor" / "-"
const char* pitchClassName(int pc);                 // "C", "C#", ...

}  // namespace ttr
