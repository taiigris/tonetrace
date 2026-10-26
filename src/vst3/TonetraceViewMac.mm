#import <Cocoa/Cocoa.h>

#include <algorithm>

#include "pluginterfaces/gui/iplugview.h"
#import "ui/TTRMainView.h"
#include "vst3/TonetraceView.h"

using namespace Steinberg;

namespace ttr {

TonetraceView::TonetraceView() : CPluginView(nullptr) {
    setRect(ViewRect(0, 0, int32(kTTRDefaultSize.width), int32(kTTRDefaultSize.height)));
}

void TonetraceView::setInitialSize(int width, int height) {
    ViewRect r(0, 0, width, height);
    checkSizeConstraint(&r);
    setRect(r);
}

TonetraceView::~TonetraceView() {
    if (nsView_) removed();
}

tresult PLUGIN_API TonetraceView::isPlatformTypeSupported(FIDString type) {
    return type && strcmp(type, kPlatformTypeNSView) == 0 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API TonetraceView::attached(void* parent, FIDString type) {
    if (!parent || !bridge_ || isPlatformTypeSupported(type) != kResultTrue) return kResultFalse;
    if (nsView_) removed();
    @autoreleasepool {
        TTRMainView_v1* view = [[TTRMainView_v1 alloc] initWithBridge:bridge_];
        NSView* host = (__bridge NSView*)parent;
        const ViewRect r = getRect();
        view.frame = NSMakeRect(0, 0, r.right - r.left, r.bottom - r.top);
        view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [host addSubview:view];
        nsView_ = (void*)CFBridgingRetain(view);
    }
    systemWindow = parent;
    if (plugFrame) resizeFromPlugin(getRect().right - getRect().left, getRect().bottom - getRect().top);
    return kResultOk;
}

tresult PLUGIN_API TonetraceView::removed() {
    if (nsView_) {
        @autoreleasepool {
            TTRMainView_v1* view = (__bridge TTRMainView_v1*)nsView_;
            [view shutdown];
            [view removeFromSuperview];
        }
        CFRelease(nsView_);
        nsView_ = nullptr;
    }
    systemWindow = nullptr;
    return kResultOk;
}

tresult PLUGIN_API TonetraceView::onSize(ViewRect* newSize) {
    if (!newSize) return kResultFalse;
    ViewRect r = *newSize;
    checkSizeConstraint(&r);
    setRect(r);
    if (nsView_) ((__bridge NSView*)nsView_).frame = NSMakeRect(0, 0, r.right - r.left, r.bottom - r.top);
    if (bridge_) {  // the window size is part of the editor state
        UiSettings ui = bridge_->uiSettings();
        ui.width = r.right - r.left;
        ui.height = r.bottom - r.top;
        bridge_->storeUiSettings(ui);
    }
    return kResultOk;
}

tresult PLUGIN_API TonetraceView::checkSizeConstraint(ViewRect* rect) {
    if (!rect) return kResultFalse;
    rect->right = rect->left + std::max<int32>(rect->right - rect->left, int32(kTTRMinimumSize.width));
    rect->bottom = rect->top + std::max<int32>(rect->bottom - rect->top, int32(kTTRMinimumSize.height));
    return kResultTrue;
}

void TonetraceView::resizeFromPlugin(int width, int height) {
    if (!plugFrame || isResizing_) return;
    isResizing_ = true;
    ViewRect r(0, 0, width, height);
    checkSizeConstraint(&r);
    if (plugFrame->resizeView(this, &r) == kResultTrue) setRect(r);
    isResizing_ = false;
}

}  // namespace ttr
