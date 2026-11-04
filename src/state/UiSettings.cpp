#include "state/UiSettings.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "state/Crc32.h"

namespace ttr {

namespace {

constexpr uint32_t kMagic = 0x32555454;  // "TTU2" (Tonetrace 5)
constexpr uint32_t kVersion = 1;
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

double clampOr(double v, double lo, double hi, double fallback) { return std::isfinite(v) ? std::clamp(v, lo, hi) : fallback; }

}  // namespace

UiSettings sanitized(UiSettings s) {
    s.pixelsPerSecond = s.pixelsPerSecond <= 0.0 || !std::isfinite(s.pixelsPerSecond) ? kDefaultPixelsPerSecond
                                                                                       : std::clamp(s.pixelsPerSecond, kMinPixelsPerSecond, kMaxPixelsPerSecond);
    s.rowHeight = clampOr(s.rowHeight, kMinRowHeight, kMaxRowHeight, kDefaultRowHeight);
    s.scrollSec = clampOr(s.scrollSec, 0.0, kMaxScrollSec, 0.0);
    s.scrollY = clampOr(s.scrollY, -1.0, kMaxScrollY, -1.0);
    s.width = std::clamp(s.width, kMinWindowWidth, kMaxWindowSize);
    s.height = std::clamp(s.height, kMinWindowHeight, kMaxWindowSize);
    return s;
}

std::vector<uint8_t> encodeUiSettings(const UiSettings& s) {
    std::vector<uint8_t> body;
    put<double>(body, s.pixelsPerSecond);
    put<double>(body, s.rowHeight);
    put<double>(body, s.scrollSec);
    put<double>(body, s.scrollY);
    put<int32_t>(body, s.width);
    put<int32_t>(body, s.height);
    put<uint8_t>(body, s.followPlayhead ? 1 : 0);
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
    if (head.get<uint32_t>() != kMagic) return fail("not a Tonetrace 5 editor state");
    const uint32_t version = head.get<uint32_t>();
    if (version != kVersion) return fail("unknown editor state version");
    const uint32_t length = head.get<uint32_t>(), crc = head.get<uint32_t>();
    if (length != size - kHeaderBytes) return fail("the editor state is truncated");
    if (crc32(data + kHeaderBytes, length) != crc) return fail("editor state checksum mismatch");
    Reader r(data + kHeaderBytes, length);
    UiSettings s;
    s.pixelsPerSecond = r.get<double>();
    s.rowHeight = r.get<double>();
    s.scrollSec = r.get<double>();
    s.scrollY = r.get<double>();
    s.width = r.get<int32_t>();
    s.height = r.get<int32_t>();
    s.followPlayhead = r.get<uint8_t>() != 0;
    if (!r.ok()) return fail("the editor state is truncated");
    *out = sanitized(s);
    return true;
}

}  // namespace ttr
