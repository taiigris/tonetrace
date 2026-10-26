// VST3 module entry: plug-in factory with the processor and controller classes.
#include "public.sdk/source/main/pluginfactory.h"
#include "vst3/TonetraceController.h"
#include "vst3/TonetraceIds.h"
#include "vst3/TonetraceProcessor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF(TTR_VST3_VENDOR, TTR_VST3_URL, TTR_VST3_EMAIL)

    DEF_CLASS2(INLINE_UID_FROM_FUID(ttr::kProcessorUid), PClassInfo::kManyInstances, kVstAudioEffectClass, TTR_VST3_NAME, 0,
               TTR_VST3_CATEGORY, TTR_VST3_VERSION, kVstVersionString, ttr::TonetraceProcessor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(ttr::kControllerUid), PClassInfo::kManyInstances, kVstComponentControllerClass, TTR_VST3_NAME " Controller", 0, "",
               TTR_VST3_VERSION, kVstVersionString, ttr::TonetraceController::createInstance)

END_FACTORY
