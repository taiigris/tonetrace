// Minimal WAV reader for the tools (read only).
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace twav {

// `seconds` of a WAV file from `startSec` as mono floats; returns the sample rate (0 on failure). PCM 16/24/32 bit and float32.
inline double readWav(const char* path, double startSec, double seconds, std::vector<float>* out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return 0.0;
    std::vector<uint8_t> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (b.size() < 44 || std::memcmp(b.data(), "RIFF", 4) != 0) return 0.0;
    auto u16 = [&](size_t p) { return uint32_t(b[p]) | uint32_t(b[p + 1]) << 8; };
    auto u32 = [&](size_t p) { return u16(p) | u16(p + 2) << 16; };
    uint32_t format = 0, channels = 0, rate = 0, bits = 0;
    size_t dataPos = 0, dataLen = 0;
    for (size_t p = 12; p + 8 <= b.size();) {
        const size_t len = u32(p + 4);
        if (std::memcmp(&b[p], "fmt ", 4) == 0) { format = u16(p + 8); channels = u16(p + 10); rate = u32(p + 12); bits = u16(p + 22); }
        if (std::memcmp(&b[p], "data", 4) == 0) { dataPos = p + 8; dataLen = std::min(len, b.size() - dataPos); break; }
        p += 8 + len + (len & 1);
    }
    if (!channels || !rate || !dataPos || (bits != 16 && bits != 24 && bits != 32)) return 0.0;
    const size_t bytes = bits / 8, total = dataLen / (bytes * channels), first = std::min(total, size_t(startSec * rate)), frames = std::min(total - first, size_t(seconds * rate));
    out->assign(frames, 0.0f);
    for (size_t f = 0; f < frames; ++f) {
        double sum = 0.0;
        for (uint32_t c = 0; c < channels; ++c) {
            const size_t p = dataPos + ((first + f) * channels + c) * bytes;
            if (bits == 16) sum += double(int16_t(u16(p))) / 32768.0;
            else if (bits == 24) sum += double(int32_t((uint32_t(b[p]) << 8 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 24)) >> 8) / 8388608.0;
            else if (format == 3) { float v; std::memcpy(&v, &b[p], 4); sum += v; }
            else sum += double(int32_t(u32(p))) / 2147483648.0;
        }
        (*out)[f] = float(sum / channels);
    }
    return double(rate);
}

}  // namespace twav
