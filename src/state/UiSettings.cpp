#include "state/UiSettings.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "state/Crc32.h"

namespace ttr {

namespace {

constexpr uint32_t kMagic = 0x31555454;  // "TTU1"
constexpr uint32_t kVersion = 2;  // 2 adds fundamentals / align / tuning trim (version 1 still loads)
constexpr uint32_t kFirstVersion = 1;
constexpr float kMaxTuningTrim = 50.0f;
constexpr size_t kHeaderBytes = 16;
constexpr double kMaxScrollSec = 24.0 * 3600.0, kMaxScrollY = 1.0e6;

template <class T> void put(std::vector<uint8_t>& b, T v) {
    const auto* p = reinterpret_cast<const uint8_t*>(&v);
    b.insert(b.end(), p, p + sizeof(T));
}

class Reader {
public:
    Reader(const uint8_t* d, size_t n) : data_(d), size_(n) {}
    template <class T> T get() {
        T v{};
        if (pos_ + sizeof(T) > size_) { ok_ = false; return v; }
        std::memcpy(&v, data_ + pos_, sizeof(T));
        pos_ += sizeof(T);
        return v;
    }
    bool ok() const { return ok_; }
private:
    const uint8_t* data_;
    size_t size_, pos_ = 0;
    bool ok_ = true;
};

float clampParam(float v, int id) { return std::isfinite(v) ? std::clamp(v, paramSpec(id).minValue, paramSpec(id).maxValue) : paramSpec(id).defaultValue; }

}  // namespace

UiSettings uiSettingsFromParams(const ParamValues& p, bool syncToHost) {
    UiSettings s;
    s.gain = p[kParamGain];
    s.range = p[kParamRange];
    s.speed = p[kParamSpeed];
    s.quickGrab = p[kParamQuickGrab];
    s.snap = int(p[kParamSnap]);
    s.hideDrums = p[kParamHideDrums] >= 0.5f;
    s.magnet = p[kParamMagnet] >= 0.5f;
    s.syncToHost = syncToHost;
    return sanitized(s);
}

void applyViewParams(const UiSettings& s, ParamValues* p) {
    (*p)[kParamGain] = s.gain;
    (*p)[kParamRange] = s.range;
    (*p)[kParamSpeed] = s.speed;
    (*p)[kParamQuickGrab] = s.quickGrab;
    (*p)[kParamSnap] = float(s.snap);
    (*p)[kParamHideDrums] = s.hideDrums ? 1.0f : 0.0f;
    (*p)[kParamMagnet] = s.magnet ? 1.0f : 0.0f;
}

UiSettings sanitized(UiSettings s) {
    s.gain = clampParam(s.gain, kParamGain);
    s.range = clampParam(s.range, kParamRange);
    s.speed = clampParam(s.speed, kParamSpeed);
    s.quickGrab = clampParam(s.quickGrab, kParamQuickGrab);
    s.snap = std::clamp(s.snap, 0, int(paramSpec(kParamSnap).maxValue));
    s.tuningTrim = std::isfinite(s.tuningTrim) ? std::clamp(s.tuningTrim, -kMaxTuningTrim, kMaxTuningTrim) : 0.0f;
    s.scrollSec = std::isfinite(s.scrollSec) ? std::clamp(s.scrollSec, 0.0, kMaxScrollSec) : 0.0;
    s.scrollY = std::isfinite(s.scrollY) ? std::clamp(s.scrollY, -1.0, kMaxScrollY) : -1.0;
    s.width = std::clamp(s.width, kMinWindowWidth, kMaxWindowSize);
    s.height = std::clamp(s.height, kMinWindowHeight, kMaxWindowSize);
    return s;
}

std::vector<uint8_t> encodeUiSettings(const UiSettings& s) {
    std::vector<uint8_t> body;
    put<float>(body, s.gain);
    put<float>(body, s.range);
    put<float>(body, s.speed);
    put<float>(body, s.quickGrab);
    put<int32_t>(body, s.snap);
    put<uint8_t>(body, s.hideDrums ? 1 : 0);
    put<uint8_t>(body, s.magnet ? 1 : 0);
    put<uint8_t>(body, s.syncToHost ? 1 : 0);
    put<double>(body, s.scrollSec);
    put<double>(body, s.scrollY);
    put<int32_t>(body, s.width);
    put<int32_t>(body, s.height);
    put<uint8_t>(body, s.fundamentals ? 1 : 0);
    put<uint8_t>(body, s.alignToSample ? 1 : 0);
    put<float>(body, s.tuningTrim);
    std::vector<uint8_t> out;
    put<uint32_t>(out, kMagic);
    put<uint32_t>(out, kVersion);
    put<uint32_t>(out, uint32_t(body.size()));
    put<uint32_t>(out, crc32(body.data(), body.size()));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool decodeUiSettings(const uint8_t* data, size_t size, UiSettings* out, std::string* reason) {
    auto fail = [&](const char* text) { if (reason) *reason = text; return false; };
    if (!data || size < kHeaderBytes) return fail("the editor state is truncated");
    Reader head(data, size);
    if (head.get<uint32_t>() != kMagic) return fail("not a Tonetrace editor state");
    const uint32_t version = head.get<uint32_t>();
    if (version < kFirstVersion || version > kVersion) return fail("unknown editor state version");
    const uint32_t length = head.get<uint32_t>(), crc = head.get<uint32_t>();
    if (length != size - kHeaderBytes) return fail("the editor state is truncated");
    if (crc32(data + kHeaderBytes, length) != crc) return fail("editor state checksum mismatch");
    Reader r(data + kHeaderBytes, length);
    UiSettings s;
    s.gain = r.get<float>();
    s.range = r.get<float>();
    s.speed = r.get<float>();
    s.quickGrab = r.get<float>();
    s.snap = r.get<int32_t>();
    s.hideDrums = r.get<uint8_t>() != 0;
    s.magnet = r.get<uint8_t>() != 0;
    s.syncToHost = r.get<uint8_t>() != 0;
    s.scrollSec = r.get<double>();
    s.scrollY = r.get<double>();
    s.width = r.get<int32_t>();
    s.height = r.get<int32_t>();
    if (version >= 2) {
        s.fundamentals = r.get<uint8_t>() != 0;
        s.alignToSample = r.get<uint8_t>() != 0;
        s.tuningTrim = r.get<float>();
    }
    if (!r.ok()) return fail("the editor state is truncated");
    *out = sanitized(s);
    return true;
}

}  // namespace ttr
