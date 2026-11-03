// View settings that belong to the editor, not to the audio state: stored in the VST3 controller state.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

#include "core/Params.h"

namespace ttr {

constexpr int kDefaultWindowWidth = 1280, kDefaultWindowHeight = 720;  // 16:9 since 4.4 (4.3 and older: 1100 x 800)
constexpr int kLegacyWindowWidth = 1100, kLegacyWindowHeight = 800;      // the old default: stored by every older editor, so it is treated as "never resized"
constexpr int kMinWindowWidth = 1024, kMinWindowHeight = 600, kMaxWindowSize = 8192;

struct UiSettings {
    // view parameters (not automatable host parameters)
    float gain = 0.5f, range = 0.5f, speed = 1.0f, quickGrab = 0.0f;
    int snap = 3;  // 1/16
    bool hideDrums = false, magnet = false, syncToHost = false;
    // thin-line map (4.1): dim overtones, shift the grid by the detected tuning, manual trim in cent
    bool fundamentals = false, alignToSample = true;
    float tuningTrim = 0.0f;
    // glow map (4.2): the waveform lane above the map can be hidden
    bool showWave = true;
    // 4.3: the full spectrogram view (false = thin lines), click-to-grab on strong lines, sampler track after Drag MIDI
    bool isSpectrum = true, grabOnClick = false, samplerOnDrag = false;
    // 4.5: Quick Grab engine (false = Classic)
    bool isNeural = false;
    // window / viewport
    double scrollSec = 0.0;
    double scrollY = -1.0;  // < 0: default (around the middle of the keyboard)
    int width = kDefaultWindowWidth, height = kDefaultWindowHeight;

    bool operator==(const UiSettings& o) const {
        return std::tie(gain, range, speed, quickGrab, snap, hideDrums, magnet, syncToHost, fundamentals, alignToSample, tuningTrim, showWave, isSpectrum, grabOnClick, samplerOnDrag, isNeural, scrollSec, scrollY, width, height) ==
               std::tie(o.gain, o.range, o.speed, o.quickGrab, o.snap, o.hideDrums, o.magnet, o.syncToHost, o.fundamentals, o.alignToSample, o.tuningTrim, o.showWave,
                        o.isSpectrum, o.grabOnClick, o.samplerOnDrag, o.isNeural, o.scrollSec, o.scrollY, o.width, o.height);
    }
};

// The view parameters of a parameter set (and the sync flag) as UiSettings; window and viewport keep their defaults.
UiSettings uiSettingsFromParams(const ParamValues& params, bool syncToHost);
// Writes the view parameters of `settings` into `params`.
void applyViewParams(const UiSettings& settings, ParamValues* params);
// Clamps every field into its valid range (also used after decoding).
UiSettings sanitized(UiSettings settings);

std::vector<uint8_t> encodeUiSettings(const UiSettings& settings);
bool decodeUiSettings(const uint8_t* data, size_t size, UiSettings* out, std::string* reason = nullptr);

}  // namespace ttr
