// Editor settings that belong to the window, not to the audio state: stored in the VST3 controller state.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

namespace ttr {

constexpr int kDefaultWindowWidth = 1280, kDefaultWindowHeight = 800;
constexpr int kMinWindowWidth = 1024, kMinWindowHeight = 640, kMaxWindowSize = 8192;
constexpr double kMinPixelsPerSecond = 4.0, kMaxPixelsPerSecond = 2000.0, kDefaultPixelsPerSecond = 0.0;  // 0 = fit the song
constexpr double kMinRowHeight = 4.0, kMaxRowHeight = 40.0, kDefaultRowHeight = 12.0;

struct UiSettings {
    double pixelsPerSecond = kDefaultPixelsPerSecond;  // horizontal zoom of waveform and piano roll (0 = whole song)
    double rowHeight = kDefaultRowHeight;              // vertical zoom of the piano roll
    double scrollSec = 0.0;
    double scrollY = -1.0;  // < 0: default (centred on the notes)
    int width = kDefaultWindowWidth, height = kDefaultWindowHeight;
    bool followPlayhead = true;

    bool operator==(const UiSettings& o) const {
        return std::tie(pixelsPerSecond, rowHeight, scrollSec, scrollY, width, height, followPlayhead) ==
               std::tie(o.pixelsPerSecond, o.rowHeight, o.scrollSec, o.scrollY, o.width, o.height, o.followPlayhead);
    }
};

// Clamps every field into its valid range (also used after decoding).
UiSettings sanitized(UiSettings settings);

std::vector<uint8_t> encodeUiSettings(const UiSettings& settings);
// Editor states of Tonetrace 4 ("TTU1") are not read: the window starts with the defaults.
bool decodeUiSettings(const uint8_t* data, size_t size, UiSettings* out, std::string* reason = nullptr);

}  // namespace ttr
