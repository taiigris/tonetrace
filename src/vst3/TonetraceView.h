// IPlugView that embeds the Cocoa TTRMainView into the host's NSView (macOS).
#pragma once
#include <functional>
#include <memory>

#include "public.sdk/source/common/pluginview.h"
#include "ui/TTRHostBridge.h"

namespace ttr {

class TonetraceView : public Steinberg::CPluginView {
public:
    TonetraceView();
    ~TonetraceView() SMTG_OVERRIDE;

    void setBridge(std::shared_ptr<TTRHostBridge> bridge) { bridge_ = std::move(bridge); }
    void setInitialSize(int width, int height);  // from the stored editor state (before the host attaches the view)
    void resizeFromPlugin(int width, int height);  // asks the host via IPlugFrame::resizeView

    Steinberg::tresult PLUGIN_API isPlatformTypeSupported(Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API attached(void* parent, Steinberg::FIDString type) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API removed() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect* newSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canResize() SMTG_OVERRIDE { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect* rect) SMTG_OVERRIDE;

private:
    std::shared_ptr<TTRHostBridge> bridge_;
    void* nsView_ = nullptr;  // retained TTRMainView (Objective-C type kept out of this header)
    bool isResizing_ = false;
};

}  // namespace ttr
