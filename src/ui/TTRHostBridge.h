// What the Cocoa UI needs from its plug-in host format (VST3). The format layer implements this once.
#pragma once
#include <memory>

#include "core/TonetraceKernel.h"
#include "state/UiSettings.h"

namespace ttr {

class TTRHostBridge {
public:
    virtual ~TTRHostBridge() = default;
    virtual std::shared_ptr<TonetraceKernel> kernel() const = 0;
    // Parameter edits made in the UI. A drag is beginEdit, many setParameter, endEdit; a click is one of each.
    virtual void beginEdit(int paramId) = 0;
    virtual void setParameter(int paramId, float value) = 0;
    virtual void endEdit(int paramId) = 0;
    // Asks the host to resize the plug-in window (VST3: IPlugFrame::resizeView). Default: ignore.
    virtual void requestResize(int /*width*/, int /*height*/) {}
    // Editor state (zoom, scroll, window size): restored when the editor opens, stored as it changes.
    virtual UiSettings uiSettings() const { return UiSettings{}; }
    virtual void storeUiSettings(const UiSettings& /*settings*/) {}
};

// Bridge that talks to the kernel only (no host involved): used by the tools.
class DirectHostBridge : public TTRHostBridge {
public:
    explicit DirectHostBridge(std::shared_ptr<TonetraceKernel> kernel) : kernel_(std::move(kernel)) {}
    std::shared_ptr<TonetraceKernel> kernel() const override { return kernel_; }
    void beginEdit(int) override {}
    void setParameter(int paramId, float value) override { kernel_->setParam(paramId, value); }
    void endEdit(int) override {}

private:
    std::shared_ptr<TonetraceKernel> kernel_;
};

}  // namespace ttr
