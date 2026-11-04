// Binary (de)serialisation of the plug-in state (Tonetrace 5): parameters, export tempo, instrument mix and the
// document (song name and path, notes with their instrument, the audio of a recording).
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Mixer.h"
#include "core/Params.h"
#include "core/Types.h"

namespace ttr {

// v6 (5.0): new parameter set, mixer, document. States of Tonetrace 4 (v3-v5) are refused with a clear reason.
constexpr uint32_t kStateFormatVersion = 6;
constexpr size_t kMaxStateAudioSamples = 4u * 1024u * 1024u;  // a recording is stored as mono 22.05 kHz (about 190 s)

struct DecodedState {
    ParamValues params = defaultParams();
    uint32_t formatVersion = 0;
    double exportBpm = 0.0;  // 0 = follow the host
    MixerState mixer;
    DocumentPtr document;  // may be null
};

std::vector<uint8_t> encodeState(const ParamValues& params, const Document* document, const MixerState& mixer, double exportBpm);
// Never throws or crashes on arbitrary input: returns false and, when `reason` is given, a readable cause.
bool decodeState(const uint8_t* data, size_t size, DecodedState* out, std::string* reason = nullptr);
// Only the parameter block (cheap; used by the VST3 controller to sync its parameters).
bool decodeStateParams(const uint8_t* data, size_t size, ParamValues* out, std::string* reason = nullptr);

}  // namespace ttr
