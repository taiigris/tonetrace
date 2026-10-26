// Stable VST3 class IDs and plug-in identity. Generated once; never change them or hosts lose saved projects.
#pragma once
#include "core/Version.h"
#include "pluginterfaces/base/funknown.h"

namespace ttr {

static const Steinberg::FUID kProcessorUid(0x2C01E9B8, 0xA004407C, 0xADB30481, 0x4388D2DB);
static const Steinberg::FUID kControllerUid(0x7BB9639E, 0x87B94D7E, 0x87FF60FF, 0x98FAFAED);

#define TTR_VST3_VENDOR "Tonetrace"
#define TTR_VST3_URL ""
#define TTR_VST3_EMAIL ""
#define TTR_VST3_NAME "Tonetrace"
#define TTR_VST3_VERSION TTR_VERSION_STRING
#define TTR_VST3_CATEGORY "Fx|Analyzer"

// Message used to hand the processor's kernel to the controller (both live in the same process).
constexpr const char* kKernelMessageId = "TonetraceKernel";
constexpr const char* kKernelRequestId = "TonetraceKernelRequest";
constexpr const char* kKernelAttr = "Handle";

}  // namespace ttr
