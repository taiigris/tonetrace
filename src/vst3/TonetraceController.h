// VST3 edit controller: parameters, state sync and the Cocoa editor view.
#pragma once
#include <memory>
#include <mutex>

#include "core/TonetraceKernel.h"
#include "state/UiSettings.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace ttr {

class TonetraceController : public Steinberg::Vst::EditController {
public:
    static Steinberg::FUnknown* createInstance(void*) { return static_cast<Steinberg::Vst::IEditController*>(new TonetraceController); }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID tag, Steinberg::Vst::ParamValue value) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    // UI-side parameter edits (called by the view's host bridge)
    void uiBeginEdit(int paramId) { if (isHostVisible(paramId)) beginEdit(Steinberg::Vst::ParamID(paramId)); }
    void uiSetParameter(int paramId, float plainValue);
    void uiEndEdit(int paramId) { if (isHostVisible(paramId)) endEdit(Steinberg::Vst::ParamID(paramId)); }
    UiSettings uiSettings();
    void storeUiSettings(const UiSettings& settings);
    void uiSetSyncToHost(bool on);

private:
    void addParameters();
    void applyToKernel();  // view parameters and sync flag of a loaded editor state -> kernel
    std::shared_ptr<TonetraceKernel> kernel_;  // owned by the processor; null until the peers are connected
    mutable std::mutex uiMutex_;
    UiSettings ui_;                  // window, viewport and the view settings as last stored / loaded
    bool hasLoadedUi_ = false;       // a state was loaded (setState / setComponentState): apply it when the kernel arrives
};

}  // namespace ttr
