// Parameter definitions shared by kernel, VST3 layer and UI (Tonetrace 5).
#pragma once
#include <array>

namespace ttr {

enum ParamId : int {
    kParamRecord = 0,     // capture the input (stops: the recording is transcribed)
    kParamMix = 1,        // playback balance: 0 = original audio only, 1 = MIDI only
    kParamVolume = 2,     // playback volume (fader, 0.8 = 0 dB)
    kParamMuteInput = 3,  // silence the incoming signal in the output
    kParamBypass = 4,
    kNumParams = 5
};

enum class ParamKind { Continuous, Indexed, Boolean };

struct ParamSpec {
    const char* name;
    ParamKind kind;
    float minValue, maxValue, defaultValue;
};

inline const ParamSpec& paramSpec(int id) {
    static const ParamSpec specs[kNumParams] = {
        {"Record", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
        {"Mix", ParamKind::Continuous, 0.0f, 1.0f, 0.5f},
        {"Volume", ParamKind::Continuous, 0.0f, 1.0f, 0.8f},
        {"Mute input", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
        {"Bypass", ParamKind::Boolean, 0.0f, 1.0f, 0.0f},
    };
    return specs[id];
}

// Offered to the host for automation (Record is a button, not an automation target).
inline bool isAutomatable(int id) { return id == kParamMix || id == kParamVolume || id == kParamMuteInput || id == kParamBypass; }
inline bool isHostVisible(int id) { return id >= 0 && id < kNumParams; }

using ParamValues = std::array<float, kNumParams>;

inline ParamValues defaultParams() {
    ParamValues v{};
    for (int i = 0; i < kNumParams; ++i) v[i] = paramSpec(i).defaultValue;
    return v;
}

}  // namespace ttr
