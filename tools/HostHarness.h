// Shared pieces of the VST3 host test: loaded plug-in instances (component + controller), process helpers, editor frame.
#pragma once
#import <Cocoa/Cocoa.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <thread>

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "public.sdk/source/vst/hosting/processdata.h"
#include "core/Params.h"
#include "state/StateCodec.h"

namespace hosttest {

using namespace Steinberg;
using namespace Steinberg::Vst;

constexpr double kRate = 44100.0;
constexpr int32 kBlock = 512;
inline int gFailures = 0;

inline void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++gFailures;
}

inline bool waitFor(const std::function<bool()>& cond, int ms = 10000) {
    for (int i = 0; i < ms / 20 && !cond(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return cond();
}

inline void pumpRunLoop(double seconds) { [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:seconds]]; }

// One loaded plug-in instance (component + controller, connected by the SDK's PlugProvider).
struct Instance {
    IPtr<PlugProvider> provider;
    IPtr<IComponent> component;
    IPtr<IEditController> controller;
    FUnknownPtr<IAudioProcessor> processor;
    HostProcessData data;
    ParameterChanges changes{8};
    int64 samplePosition = 0;
    ProcessContext context{};
    double sampleRate = kRate;
    int32 maxBlock = kBlock;
    bool isPlaying = false;

    bool open(const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& info, double rate = kRate, int32 block = kBlock,
              int32 mode = kRealtime, int32 channels = 2) {
        sampleRate = rate;
        maxBlock = block;
        provider = owned(new PlugProvider(factory, info, false));
        if (!provider->initialize()) { printf("    PlugProvider::initialize failed\n"); return false; }
        component = owned(provider->getComponent());     // creates the pair on first use and connects them
        controller = owned(provider->getController());
        processor = FUnknownPtr<IAudioProcessor>(component);
        if (!processor || !controller) { printf("    missing: component=%p processor=%p controller=%p\n", (void*)component.get(), (void*)processor.get(), (void*)controller.get()); return false; }
        SpeakerArrangement arrangement = channels == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        if (processor->setBusArrangements(&arrangement, 1, &arrangement, 1) != kResultOk) { printf("    setBusArrangements failed\n"); return false; }
        component->activateBus(kAudio, kInput, 0, true);
        component->activateBus(kAudio, kOutput, 0, true);
        ProcessSetup setup{mode, kSample32, block, rate};
        if (processor->setupProcessing(setup) != kResultOk || component->setActive(true) != kResultOk) { printf("    setup/active failed\n"); return false; }
        if (processor->setProcessing(true) != kResultOk) { printf("    setProcessing(true) did not return kResultOk\n"); return false; }
        if (!data.prepare(*component, block, kSample32)) { printf("    HostProcessData::prepare failed\n"); return false; }
        data.processContext = &context;
        context.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
        context.tempo = 100.0;
        context.projectTimeMusic = 8.0;
        return true;
    }

    void close() {
        if (processor) processor->setProcessing(false);
        if (component) component->setActive(false);
        data.unprepare();
        provider.reset();
    }

    // Processes `frames` samples of `left` (right = 0.5 * left, null = silence); appends the output channels.
    bool process(const float* left, std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr, int32 frames = kBlock) {
        const int channels = data.inputs[0].numChannels;
        for (int32 i = 0; i < frames; ++i) {
            data.inputs[0].channelBuffers32[0][i] = left ? left[i] : 0.0f;
            if (channels > 1) data.inputs[0].channelBuffers32[1][i] = left ? 0.5f * left[i] : 0.0f;
        }
        data.numSamples = frames;
        data.inputParameterChanges = &changes;
        context.state = (context.state & ~ProcessContext::kPlaying) | (isPlaying ? ProcessContext::kPlaying : 0);
        const bool ok = processor->process(data) == kResultOk;
        changes.clearQueue();
        if (outL) outL->insert(outL->end(), data.outputs[0].channelBuffers32[0], data.outputs[0].channelBuffers32[0] + frames);
        if (outR && channels > 1) outR->insert(outR->end(), data.outputs[0].channelBuffers32[1], data.outputs[0].channelBuffers32[1] + frames);
        context.projectTimeMusic += double(frames) / sampleRate * context.tempo / 60.0;
        return ok;
    }

    void setParam(ParamID id, double normalized) {
        int32 index = 0;
        if (IParamValueQueue* q = changes.addParameterData(id, index)) q->addPoint(0, normalized, index);
    }

    ttr::DecodedState state() {
        MemoryStream stream;
        ttr::DecodedState decoded;
        if (component->getState(&stream) == kResultOk)
            ttr::decodeState(reinterpret_cast<const uint8_t*>(stream.getData()), size_t(stream.getSize()), &decoded);
        return decoded;
    }
};


inline double rms(const std::vector<float>& v) {
    double sum = 0;
    for (float x : v) sum += double(x) * x;
    return std::sqrt(sum / double(std::max<size_t>(1, v.size())));
}

// Frame that records the resize requests of the editor.
class TestFrame : public IPlugFrame {
public:
    TestFrame() { FUNKNOWN_CTOR }
    virtual ~TestFrame() { FUNKNOWN_DTOR }
    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* size) SMTG_OVERRIDE {
        ++calls;
        last = *size;
        return view->onSize(size);
    }
    DECLARE_FUNKNOWN_METHODS
    int calls = 0;
    ViewRect last;
};

// Memory stream positioned at the start (for reading back what a getState wrote).
inline void rewind(MemoryStream& stream) {
    int64 pos = 0;
    stream.seek(0, IBStream::kIBSeekSet, &pos);
}

// Stress and robustness sections (HostStress.mm).
void runStressTests(const VST3::Hosting::PluginFactory& factory, const VST3::Hosting::ClassInfo& audioClass, const std::vector<float>& signal);

}  // namespace hosttest
