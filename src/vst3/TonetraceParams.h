// Mapping between the kernel's plain parameter values and VST3 normalised values.
#pragma once
#include <algorithm>

#include "core/Params.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace ttr {

inline Steinberg::Vst::ParamValue toNormalized(int id, float plain) {
    const ParamSpec& s = paramSpec(id);
    return s.maxValue > s.minValue ? std::clamp((plain - s.minValue) / (s.maxValue - s.minValue), 0.0f, 1.0f) : 0.0;
}

inline float toPlain(int id, Steinberg::Vst::ParamValue normalized) {
    const ParamSpec& s = paramSpec(id);
    return s.minValue + float(std::clamp(normalized, 0.0, 1.0)) * (s.maxValue - s.minValue);
}

}  // namespace ttr
