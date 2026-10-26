#import "ui/TTRViewportInput.h"

const double kTTRMinRowHeight = 8.0, kTTRMaxRowHeight = 28.0;

static const double kPreciseZoomPerPoint = 0.01, kWheelZoomPerLine = 0.08, kWheelLinePoints = 12.0, kEndMarginSec = 2.0;

void TTRClampViewport(TTRViewport* vp, NSSize visible) {
    vp->rowH = MIN(kTTRMaxRowHeight, MAX(kTTRMinRowHeight, vp->rowH));
    vp->scrollY = MIN(MAX(0.0, vp->scrollY), MAX(0.0, vp->contentHeight() - visible.height));
    const double visibleSec = visible.width / vp->pxPerSec;
    vp->scrollSec = MIN(MAX(0.0, vp->scrollSec), MAX(0.0, vp->contentSec + kEndMarginSec - visibleSec));
}

static void zoomTime(TTRViewport* vp, double factor, NSPoint anchor, void (^onZoom)(double)) {
    const double anchorSec = vp->timeForX(anchor.x);
    const double speed = MIN(double(ttr::kSpeedMax), MAX(double(ttr::kSpeedMin), vp->pxPerSec / ttr::kBasePixelsPerSecond * factor));
    vp->pxPerSec = speed * ttr::kBasePixelsPerSecond;
    vp->scrollSec = anchorSec - anchor.x / vp->pxPerSec;
    if (onZoom) onZoom(speed);
}

BOOL TTRHandleWheel(NSEvent* e, TTRViewport* vp, NSPoint anchor, NSSize visible, BOOL allowVertical, void (^onZoom)(double)) {
    const BOOL precise = e.hasPreciseScrollingDeltas;
    const double scale = precise ? 1.0 : kWheelLinePoints;
    const double dx = e.scrollingDeltaX * scale, dy = e.scrollingDeltaY * scale;
    const NSEventModifierFlags flags = e.modifierFlags;
    if (flags & NSEventModifierFlagCommand) {
        zoomTime(vp, exp((precise ? kPreciseZoomPerPoint : kWheelZoomPerLine / kWheelLinePoints) * (dy != 0 ? dy : dx)), anchor, onZoom);
    } else if (flags & NSEventModifierFlagOption) {
        if (!allowVertical) return NO;
        const double anchorPitch = vp->pitchForY(anchor.y);
        vp->rowH *= exp(kPreciseZoomPerPoint * dy);
        vp->rowH = MIN(kTTRMaxRowHeight, MAX(kTTRMinRowHeight, vp->rowH));
        vp->scrollY = (ttr::kMaxNote + 0.5 - anchorPitch) * vp->rowH - anchor.y;  // keep the pitch under the pointer
    } else if (flags & NSEventModifierFlagShift) {
        vp->scrollSec -= (dx != 0 ? dx : dy) / vp->pxPerSec;
    } else {
        if (allowVertical) vp->scrollY -= dy;
        else if (dx == 0) vp->scrollSec -= dy / vp->pxPerSec;  // the lane has no vertical axis: the wheel pans in time
        vp->scrollSec -= dx / vp->pxPerSec;
    }
    TTRClampViewport(vp, visible);
    return YES;
}

void TTRHandlePinch(NSEvent* e, TTRViewport* vp, NSPoint anchor, NSSize visible, void (^onZoom)(double)) {
    zoomTime(vp, 1.0 + e.magnification, anchor, onZoom);
    TTRClampViewport(vp, visible);
}
