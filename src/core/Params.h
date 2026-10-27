// Parameter definitions shared by kernel, VST3 layer and UI.
#pragma once
#include <array>

namespace ttr {

enum ParamId : int {
    kParamRecord = 0,
    kParamGain = 1,
    kParamRange = 2,
    kParamSpeed = 3,
    kParamHideDrums = 4,
    kParamSnap = 5,
    kParamMagnet = 6,
    kParamQuickGrab = 7,
    kParamSoundVolume = 8,
    kParamMix = 9,
    kParamSolo = 10,
    kParamPreview = 11,
    kParamBypass = 12,
    kNumParams = 13
};

enum class ParamKind { Continuous, Indexed, Boolean };

struct ParamSpec {
    const char* name;
    ParamKind kind;
    float minValue, maxValue, defaultValue;
};

constexpr float kSpeedMin = 0.25f, kSpeedMax = 8.0f;
constexpr double kBasePixelsPerSecond = 100.0;  // time zoom 1x

inline const ParamSpec& paramSpec(int id) {
    static const ParamSpec specs[kNumParams] = {
        {"Record", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
        {"Gain", ParamKind::Continuous, 0.0f, 1.0f, 0.5f},
        {"Range", ParamKind::Continuous, 0.0f, 1.0f, 0.5f},
        {"Speed", ParamKind::Continuous, kSpeedMin, kSpeedMax, 1.0f},
        {"Hide drums", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
        {"Snap", ParamKind::Indexed, 0.0f, 5.0f, 3.0f},
        {"Magnet", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
        {"Quick Grab", ParamKind::Continuous, 0.0f, 1.0f, 0.0f},
        {"Sound", ParamKind::Continuous, 0.0f, 1.0f, 0.7f},
        {"Mix", ParamKind::Continuous, 0.0f, 1.0f, 0.5f},
        {"Solo", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
        {"Preview", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
        {"Bypass", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
    };
    return specs[id];
}

// Only these are offered to the host for automation; the others are view settings or the recording switch and stay
// out of Live's automation list. The ids of all parameters are stable (saved projects depend on them).
inline bool isAutomatable(int id) {
    return id == kParamPreview || id == kParamSoundVolume || id == kParamMix || id == kParamSolo || id == kParamBypass;
}
// Reaches the processor through the host (Record), as opposed to pure view settings kept in the editor state.
inline bool isHostVisible(int id) { return isAutomatable(id) || id == kParamRecord; }

using ParamValues = std::array<float, kNumParams>;

inline ParamValues defaultParams() {
    ParamValues v{};
    for (int i = 0; i < kNumParams; ++i) v[i] = paramSpec(i).defaultValue;
    return v;
}

}  // namespace ttr
