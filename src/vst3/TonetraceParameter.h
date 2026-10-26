// Parameter flavours with readable value strings ("On"/"Off", "60%") that parse back to the same value.
#pragma once
#include <cstdlib>
#include <cstring>

#include "pluginterfaces/base/ustring.h"
#include "public.sdk/source/vst/vstparameters.h"

namespace ttr {

class ToggleParameter : public Steinberg::Vst::Parameter {
public:
    ToggleParameter(const Steinberg::Vst::TChar* title, Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue defaultNormalized,
                    Steinberg::int32 flags = Steinberg::Vst::ParameterInfo::kCanAutomate)
        : Parameter(title, tag, nullptr, defaultNormalized, 1, flags) {}

    void toString(Steinberg::Vst::ParamValue normalized, Steinberg::Vst::String128 string) const SMTG_OVERRIDE {
        Steinberg::UString(string, 128).fromAscii(normalized >= 0.5 ? "On" : "Off");
    }
    bool fromString(const Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& normalized) const SMTG_OVERRIDE {
        char text[32] = {};
        Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(string), 128).toAscii(text, sizeof text);
        if (!strcasecmp(text, "on") || !strcmp(text, "1")) { normalized = 1.0; return true; }
        if (!strcasecmp(text, "off") || !strcmp(text, "0")) { normalized = 0.0; return true; }
        return false;
    }
};

class PercentParameter : public Steinberg::Vst::Parameter {
public:
    PercentParameter(const Steinberg::Vst::TChar* title, Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue defaultNormalized,
                     Steinberg::int32 flags = Steinberg::Vst::ParameterInfo::kCanAutomate)
        : Parameter(title, tag, nullptr, defaultNormalized, 0, flags) {}

    void toString(Steinberg::Vst::ParamValue normalized, Steinberg::Vst::String128 string) const SMTG_OVERRIDE {
        char text[32];
        snprintf(text, sizeof text, "%.0f%%", normalized * 100.0);
        Steinberg::UString(string, 128).fromAscii(text);
    }
    bool fromString(const Steinberg::Vst::TChar* string, Steinberg::Vst::ParamValue& normalized) const SMTG_OVERRIDE {
        char text[32] = {};
        Steinberg::UString(const_cast<Steinberg::Vst::TChar*>(string), 128).toAscii(text, sizeof text);
        char* end = nullptr;
        const double v = strtod(text, &end);
        if (end == text) return false;
        normalized = v / 100.0 < 0.0 ? 0.0 : (v / 100.0 > 1.0 ? 1.0 : v / 100.0);
        return true;
    }
};

}  // namespace ttr
