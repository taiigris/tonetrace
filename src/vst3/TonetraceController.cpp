#include "vst3/TonetraceController.h"

#include <cstring>
#include <functional>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "public.sdk/source/vst/vstparameters.h"
#include "core/NoteEdit.h"
#include "state/StateCodec.h"
#include "core/Logger.h"
#include "vst3/StreamIo.h"
#include "vst3/TonetraceIds.h"
#include "vst3/TonetraceParameter.h"
#include "vst3/TonetraceParams.h"
#include "vst3/TonetraceView.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace ttr {

namespace {

constexpr int kBypassFlags = ParameterInfo::kCanAutomate | ParameterInfo::kIsBypass;

// Bridge handed to the Cocoa UI: edits go through the controller so the host records automation.
class ControllerBridge : public TTRHostBridge {
public:
    ControllerBridge(IPtr<TonetraceController> controller, std::shared_ptr<TonetraceKernel> kernel, std::function<void(int, int)> resize)
        : controller_(std::move(controller)), kernel_(std::move(kernel)), resize_(std::move(resize)) {}
    std::shared_ptr<TonetraceKernel> kernel() const override { return kernel_; }
    void beginEdit(int id) override { controller_->uiBeginEdit(id); }
    void setParameter(int id, float value) override { controller_->uiSetParameter(id, value); }
    void endEdit(int id) override { controller_->uiEndEdit(id); }
    void requestResize(int w, int h) override { if (resize_) resize_(w, h); }
    UiSettings uiSettings() const override { return controller_->uiSettings(); }
    void storeUiSettings(const UiSettings& settings) override { controller_->storeUiSettings(settings); }
    void setSyncToHost(bool on) override { controller_->uiSetSyncToHost(on); }

private:
    IPtr<TonetraceController> controller_;
    std::shared_ptr<TonetraceKernel> kernel_;
    std::function<void(int, int)> resize_;
};

}  // namespace

namespace {
// Automation targets keep kCanAutomate. View settings (and the record switch) are hidden and not automatable, so
// the host's automation list stays clean; they are saved in the processor and controller state instead.
int32 viewFlags(int id) { return isAutomatable(id) ? int32(ParameterInfo::kCanAutomate) : int32(ParameterInfo::kIsHidden); }
}  // namespace

void TonetraceController::addParameters() {
    for (int id = 0; id < kNumParams; ++id) {
        const ParamSpec& spec = paramSpec(id);
        TChar title[128];
        UString128(spec.name).copyTo(title, 128);
        const ParamValue defaultNormalized = toNormalized(id, spec.defaultValue);
        if (spec.kind == ParamKind::Indexed) {  // Snap
            auto* list = new StringListParameter(title, ParamID(id));
            for (int i = 0; i < kNumSnapModes; ++i) {
                TChar label[128];
                UString128(snapName(i)).copyTo(label, 128);
                list->appendString(label);
            }
            list->getInfo().defaultNormalizedValue = defaultNormalized;
            list->getInfo().flags = (list->getInfo().flags & ~ParameterInfo::kCanAutomate) | viewFlags(id);
            list->setNormalized(defaultNormalized);
            parameters.addParameter(list);
        } else if (spec.kind == ParamKind::Boolean) {
            const int32 flags = id == kParamBypass ? kBypassFlags : viewFlags(id);
            parameters.addParameter(new ToggleParameter(title, ParamID(id), defaultNormalized, flags));
        } else if (id == kParamSpeed) {
            parameters.addParameter(new RangeParameter(title, ParamID(id), STR16("x"), spec.minValue, spec.maxValue, spec.defaultValue, 0, viewFlags(id)));
        } else {
            parameters.addParameter(new PercentParameter(title, ParamID(id), defaultNormalized, viewFlags(id)));
        }
    }
}

tresult PLUGIN_API TonetraceController::initialize(FUnknown* context) {
    const tresult result = EditController::initialize(context);
    if (result != kResultOk) return result;
    addParameters();
    return kResultOk;
}

tresult PLUGIN_API TonetraceController::terminate() {
    kernel_.reset();
    return EditController::terminate();
}

// Processor state (also the v3 format): sync the controller's parameters and the view settings with it.
tresult PLUGIN_API TonetraceController::setComponentState(IBStream* state) {
    if (!state) return kResultFalse;
    std::vector<uint8_t> bytes;
    DecodedState decoded;
    std::string reason;
    if (!readAll(state, &bytes) || !decodeState(bytes.data(), bytes.size(), &decoded, &reason)) {
        logWarn(kernel_ ? kernel_->instanceId() : 0, "controller: component state not usable: %s", reason.empty() ? "stream error" : reason.c_str());
        return kResultFalse;
    }
    for (int id = 0; id < kNumParams; ++id) EditController::setParamNormalized(ParamID(id), toNormalized(id, decoded.params[size_t(id)]));
    std::lock_guard<std::mutex> lock(uiMutex_);
    const UiSettings fromState = uiSettingsFromParams(decoded.params, decoded.syncToHost);
    UiSettings merged = ui_;  // keep the window and viewport of an editor state that was loaded before
    merged.gain = fromState.gain; merged.range = fromState.range; merged.speed = fromState.speed; merged.quickGrab = fromState.quickGrab;
    merged.snap = fromState.snap; merged.hideDrums = fromState.hideDrums; merged.magnet = fromState.magnet; merged.syncToHost = fromState.syncToHost;
    ui_ = merged;
    return kResultOk;
}

// Editor state: view settings, zoom / scroll and the window size (never automation, never audio data).
tresult PLUGIN_API TonetraceController::getState(IBStream* state) {
    if (!state) return kResultFalse;
    return writeAll(state, encodeUiSettings(uiSettings())) ? kResultOk : kResultFalse;
}

tresult PLUGIN_API TonetraceController::setState(IBStream* state) {
    if (!state) return kResultFalse;
    std::vector<uint8_t> bytes;
    UiSettings loaded;
    std::string reason;
    if (!readAll(state, &bytes) || !decodeUiSettings(bytes.data(), bytes.size(), &loaded, &reason)) {
        logWarn(kernel_ ? kernel_->instanceId() : 0, "controller: editor state not usable: %s", reason.empty() ? "stream error" : reason.c_str());
        return kResultFalse;
    }
    {
        std::lock_guard<std::mutex> lock(uiMutex_);
        ui_ = loaded;
        hasLoadedUi_ = true;
    }
    ParamValues params = defaultParams();
    applyViewParams(loaded, &params);
    for (int id = 0; id < kNumParams; ++id)
        if (!isHostVisible(id)) EditController::setParamNormalized(ParamID(id), toNormalized(id, params[size_t(id)]));
    applyToKernel();
    return kResultOk;
}

void TonetraceController::applyToKernel() {
    UiSettings s;
    {
        std::lock_guard<std::mutex> lock(uiMutex_);
        if (!hasLoadedUi_) return;
        s = ui_;
    }
    if (!kernel_) return;  // applied when the processor hands over its kernel
    ParamValues params = kernel_->allParams();
    applyViewParams(s, &params);
    for (int id = 0; id < kNumParams; ++id)
        if (!isHostVisible(id)) kernel_->setParam(id, params[size_t(id)]);
    kernel_->setSyncToHost(s.syncToHost);
}

// The view settings follow the live parameter values; window and viewport are what the editor stored last.
UiSettings TonetraceController::uiSettings() {
    UiSettings s;
    {
        std::lock_guard<std::mutex> lock(uiMutex_);
        s = ui_;
    }
    s.gain = toPlain(kParamGain, getParamNormalized(kParamGain));
    s.range = toPlain(kParamRange, getParamNormalized(kParamRange));
    s.speed = toPlain(kParamSpeed, getParamNormalized(kParamSpeed));
    s.quickGrab = toPlain(kParamQuickGrab, getParamNormalized(kParamQuickGrab));
    s.snap = int(std::lround(toPlain(kParamSnap, getParamNormalized(kParamSnap))));
    s.hideDrums = getParamNormalized(kParamHideDrums) >= 0.5;
    s.magnet = getParamNormalized(kParamMagnet) >= 0.5;
    if (kernel_) s.syncToHost = kernel_->syncToHost();
    return sanitized(s);
}

void TonetraceController::storeUiSettings(const UiSettings& settings) {
    std::lock_guard<std::mutex> lock(uiMutex_);
    ui_ = sanitized(settings);
}

void TonetraceController::uiSetSyncToHost(bool on) {
    if (kernel_) kernel_->setSyncToHost(on);
    std::lock_guard<std::mutex> lock(uiMutex_);
    ui_.syncToHost = on;
}

tresult PLUGIN_API TonetraceController::setParamNormalized(ParamID tag, ParamValue value) {
    const tresult result = EditController::setParamNormalized(tag, value);
    // The kernel's parameters are atomics: mirror the value now so the UI and the audio side agree even
    // before the host's next process() call delivers the change.
    if (kernel_ && tag < ParamID(kNumParams)) kernel_->setParam(int(tag), toPlain(int(tag), value));
    return result;
}

void TonetraceController::uiSetParameter(int paramId, float plainValue) {
    if (paramId < 0 || paramId >= kNumParams) return;
    const ParamValue normalized = toNormalized(paramId, plainValue);
    setParamNormalized(ParamID(paramId), normalized);
    if (isHostVisible(paramId)) performEdit(ParamID(paramId), normalized);  // view settings never reach the host
}

IPlugView* PLUGIN_API TonetraceController::createView(FIDString name) {
    if (!name || std::strcmp(name, ViewType::kEditor) != 0) return nullptr;
    if (!kernel_) kernel_ = std::make_shared<TonetraceKernel>();  // not connected to a processor: empty, inert editor
    IPtr<TonetraceController> self(this);
    auto* view = new TonetraceView();
    const UiSettings ui = uiSettings();
    view->setInitialSize(ui.width, ui.height);
    view->setBridge(std::make_shared<ControllerBridge>(self, kernel_, [view](int w, int h) { view->resizeFromPlugin(w, h); }));
    return view;
}

tresult PLUGIN_API TonetraceController::connect(IConnectionPoint* other) {
    const tresult result = EditController::connect(other);
    if (result == kResultOk && !kernel_) {  // ask the processor for its kernel (covers either connect order)
        if (IMessage* message = allocateMessage()) {
            FReleaser releaser(message);
            message->setMessageID(kKernelRequestId);
            sendMessage(message);
        }
    }
    return result;
}

tresult PLUGIN_API TonetraceController::notify(IMessage* message) {
    if (message && std::strcmp(message->getMessageID(), kKernelMessageId) == 0) {
        int64 raw = 0;
        if (message->getAttributes()->getInt(kKernelAttr, raw) != kResultOk || raw == 0) return kResultFalse;
        std::unique_ptr<std::shared_ptr<TonetraceKernel>> handle(reinterpret_cast<std::shared_ptr<TonetraceKernel>*>(intptr_t(raw)));
        kernel_ = *handle;
        applyToKernel();
        return kResultOk;
    }
    return EditController::notify(message);
}

}  // namespace ttr
