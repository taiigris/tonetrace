#include "vst3/TonetraceProcessor.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include "pluginterfaces/base/ustring.h"
#include "core/Logger.h"
#include "vst3/StreamIo.h"
#include "vst3/TonetraceIds.h"
#include "vst3/TonetraceParams.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace ttr {

namespace {

constexpr int kMaxChannels = 2;

bool isSupportedPair(SpeakerArrangement in, SpeakerArrangement out) {
    return in == out && (in == SpeakerArr::kMono || in == SpeakerArr::kStereo);
}

}  // namespace

TonetraceProcessor::TonetraceProcessor() : kernel_(std::make_shared<TonetraceKernel>()) {
    setControllerClass(kControllerUid);
    processContextRequirements.flags = IProcessContextRequirements::kNeedTempo | IProcessContextRequirements::kNeedProjectTimeMusic;
}

tresult PLUGIN_API TonetraceProcessor::initialize(FUnknown* context) {
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) return result;
    addAudioInput(STR16("Stereo In"), SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);
    kernel_->setChannels(2);
    if (FUnknownPtr<IHostApplication> host{context}) {
        String128 name{};
        if (host->getName(name) == kResultOk) {
            char utf8[128] = {};
            UString(name, 128).toAscii(utf8, sizeof utf8);
            kernel_->setHostName(utf8);
        }
    }
    return kResultOk;
}

tresult PLUGIN_API TonetraceProcessor::terminate() {
    kernel_->deactivate();
    return AudioEffect::terminate();
}

tresult PLUGIN_API TonetraceProcessor::setBusArrangements(SpeakerArrangement* inputs, int32 numIns, SpeakerArrangement* outputs, int32 numOuts) {
    if (numIns == 1 && numOuts == 1 && isSupportedPair(inputs[0], outputs[0])) {
        kernel_->setChannels(SpeakerArr::getChannelCount(inputs[0]));
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }
    // Anything else: stay stereo -> stereo (the host reads it back with getBusArrangement).
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    AudioEffect::setBusArrangements(&stereo, 1, &stereo, 1);
    return kResultFalse;
}

tresult PLUGIN_API TonetraceProcessor::canProcessSampleSize(int32 symbolicSampleSize) {
    return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API TonetraceProcessor::setupProcessing(ProcessSetup& setup) {
    sampleRate_ = setup.sampleRate;
    maxBlock_ = std::max<int>(1, setup.maxSamplesPerBlock);
    kernel_->setOffline(setup.processMode == kOffline);
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API TonetraceProcessor::setProcessing(TBool state) {
    kernel_->setProcessing(state != 0);
    return kResultOk;
}

tresult PLUGIN_API TonetraceProcessor::setActive(TBool state) {
    if (state) kernel_->prepare(sampleRate_, maxBlock_);  // allocates and starts the analysis worker
    else kernel_->deactivate();
    return AudioEffect::setActive(state);
}

void TonetraceProcessor::applyParameterChanges(IParameterChanges* changes) {
    if (!changes) return;
    const int32 count = changes->getParameterCount();
    for (int32 i = 0; i < count; ++i) {
        IParamValueQueue* queue = changes->getParameterData(i);
        const int32 points = queue ? queue->getPointCount() : 0;
        const ParamID id = queue ? queue->getParameterId() : 0;
        if (points <= 0 || id >= ParamID(kNumParams)) continue;
        int32 offset = 0;
        ParamValue value = 0.0;
        if (queue->getPoint(points - 1, offset, value) == kResultTrue) kernel_->setParam(int(id), toPlain(int(id), value));
    }
}

tresult PLUGIN_API TonetraceProcessor::process(ProcessData& data) {
    applyParameterChanges(data.inputParameterChanges);
    if (const ProcessContext* ctx = data.processContext) {
        const bool isPlaying = (ctx->state & ProcessContext::kPlaying) != 0;
        if ((ctx->state & ProcessContext::kTempoValid) && (ctx->state & ProcessContext::kProjectTimeMusicValid))
            kernel_->setHostTransport(ctx->tempo, ctx->projectTimeMusic, isPlaying);
        else if (ctx->state & ProcessContext::kTempoValid)
            kernel_->setHostTransport(ctx->tempo, 0.0, isPlaying);
    }
    if (data.numSamples <= 0 || data.numInputs < 1 || data.numOutputs < 1) return kResultOk;

    AudioBusBuffers& in = data.inputs[0];
    AudioBusBuffers& out = data.outputs[0];
    const int channels = std::min<int>({in.numChannels, out.numChannels, kMaxChannels});
    if (channels <= 0 || !in.channelBuffers32 || !out.channelBuffers32) return kResultOk;
    out.silenceFlags = 0;
    for (int c = 0; c < channels; ++c)
        if (out.channelBuffers32[c] != in.channelBuffers32[c])
            std::memcpy(out.channelBuffers32[c], in.channelBuffers32[c], size_t(data.numSamples) * sizeof(float));

    // The kernel works in place on the output; split hosts' oversized blocks without allocating.
    int done = 0;
    while (done < data.numSamples) {
        const int frames = std::min(maxBlock_, int(data.numSamples) - done);
        float* chunk[kMaxChannels] = {};
        for (int c = 0; c < channels; ++c) chunk[c] = out.channelBuffers32[c] + done;
        kernel_->process(chunk, channels, frames);
        done += frames;
    }
    return kResultOk;
}

tresult PLUGIN_API TonetraceProcessor::setState(IBStream* state) {
    if (!state) return kResultFalse;
    std::vector<uint8_t> bytes;
    if (!readAll(state, &bytes)) {
        kernel_->notice("The saved state could not be loaded: it is larger than the allowed maximum", NoticeLevel::Error);
        logError(kernel_->instanceId(), "setState: stream larger than %zu bytes", kMaxStateBytes);
        return kResultFalse;
    }
    return kernel_->loadState(bytes.data(), bytes.size()) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API TonetraceProcessor::getState(IBStream* state) {
    if (!state) return kResultFalse;
    const std::vector<uint8_t> bytes = kernel_->saveState();
    if (!writeAll(state, bytes)) {
        logError(kernel_->instanceId(), "getState: writing %zu bytes failed", bytes.size());
        return kResultFalse;
    }
    return kResultOk;
}

void TonetraceProcessor::sendKernelToController() {
    IMessage* message = allocateMessage();
    if (!message) return;
    FReleaser releaser(message);
    message->setMessageID(kKernelMessageId);
    // The controller takes ownership of this heap handle (same process only).
    auto* handle = new std::shared_ptr<TonetraceKernel>(kernel_);
    message->getAttributes()->setInt(kKernelAttr, int64(reinterpret_cast<intptr_t>(handle)));
    if (sendMessage(message) != kResultOk) delete handle;
}

tresult PLUGIN_API TonetraceProcessor::connect(IConnectionPoint* other) {
    const tresult result = AudioEffect::connect(other);
    if (result == kResultOk) sendKernelToController();
    return result;
}

tresult PLUGIN_API TonetraceProcessor::notify(IMessage* message) {
    if (message && std::strcmp(message->getMessageID(), kKernelRequestId) == 0) {
        sendKernelToController();
        return kResultOk;
    }
    return AudioEffect::notify(message);
}

}  // namespace ttr
