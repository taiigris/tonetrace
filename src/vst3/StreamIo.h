// IBStream helpers shared by the processor and the controller.
#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

#include "pluginterfaces/base/ibstream.h"

namespace ttr {

constexpr size_t kMaxStateBytes = 64u << 20;

// Reads the whole stream. False when it is larger than kMaxStateBytes or a read fails.
inline bool readAll(Steinberg::IBStream* stream, std::vector<uint8_t>* bytes) {
    constexpr Steinberg::int32 kChunk = 1 << 16;
    bytes->clear();
    std::vector<uint8_t> chunk(kChunk);
    for (;;) {
        Steinberg::int32 read = 0;
        if (stream->read(chunk.data(), kChunk, &read) != Steinberg::kResultOk || read <= 0) break;
        bytes->insert(bytes->end(), chunk.begin(), chunk.begin() + read);
        if (bytes->size() > kMaxStateBytes) return false;
    }
    return true;
}

inline bool writeAll(Steinberg::IBStream* stream, const std::vector<uint8_t>& bytes) {
    constexpr size_t kChunk = 1 << 16;
    size_t pos = 0;
    while (pos < bytes.size()) {
        Steinberg::int32 written = 0;
        const auto want = Steinberg::int32(std::min(bytes.size() - pos, kChunk));
        if (stream->write(const_cast<uint8_t*>(bytes.data()) + pos, want, &written) != Steinberg::kResultOk || written <= 0) return false;
        pos += size_t(written);
    }
    return true;
}

}  // namespace ttr
