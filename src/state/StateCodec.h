// Binary (de)serialisation of the plugin state: parameters, drawn notes, spectrogram, waveform peaks and (short) audio.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Params.h"
#include "core/Types.h"

namespace ttr {

constexpr size_t kMaxSpectrogramStateBytes = 4u * 1024u * 1024u;    // quantised spectrogram

constexpr uint32_t kStateFormatVersion = 4;  // v4: size + CRC32 header, "sync to host" flag. v3 states still load.

struct DecodedState {
    ParamValues params = defaultParams();
    bool syncToHost = false;
    uint32_t formatVersion = 0;
    SnapshotPtr snapshot;  // may be null
};

std::vector<uint8_t> encodeState(const ParamValues& params, const Snapshot* snapshot, bool syncToHost = false);
// Never throws or crashes on arbitrary input: returns false and, when `reason` is given, a readable cause.
bool decodeState(const uint8_t* data, size_t size, DecodedState* out, std::string* reason = nullptr);
// Only the parameter block (cheap; used by the VST3 controller to sync its parameters).
bool decodeStateParams(const uint8_t* data, size_t size, ParamValues* out, std::string* reason = nullptr);

}  // namespace ttr
