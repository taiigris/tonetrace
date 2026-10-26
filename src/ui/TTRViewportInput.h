// Scroll wheel / trackpad / pinch handling for the shared time window (used by the lane and the map).
#pragma once
#import <Cocoa/Cocoa.h>

#include "ui/TTRViewport.h"

extern const double kTTRMinRowHeight, kTTRMaxRowHeight;

// Keeps the window inside the content. visibleSize = size of the map area in points.
void TTRClampViewport(TTRViewport* vp, NSSize visibleSize);

// Wheel: vertical scroll; Shift = horizontal; Cmd = time zoom around `anchor`; Alt = row height zoom.
// `onZoom` receives the new Speed value (1.0 = 100 px per second). Returns YES when the view changed.
BOOL TTRHandleWheel(NSEvent* event, TTRViewport* vp, NSPoint anchor, NSSize visibleSize, BOOL allowVertical, void (^onZoom)(double speed));

// Pinch gesture: time zoom around `anchor`.
void TTRHandlePinch(NSEvent* event, TTRViewport* vp, NSPoint anchor, NSSize visibleSize, void (^onZoom)(double speed));
