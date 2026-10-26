// Record button, crosshair info box and scroll bar.
#import <QuartzCore/QuartzCore.h>

#include <algorithm>

#import "ui/TTRControls.h"
#import "ui/TTRTheme.h"

// ============================================================ TTRRecordButton_v1

static const double kPulseHz = 1.3;

@implementation TTRRecordButton_v1 {
    BOOL _hover, _pressed;
    NSTrackingArea* _tracking;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _isEnabled = YES;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)setIsOn:(BOOL)on { if (on != _isOn) { _isOn = on; self.needsDisplay = YES; } if (on) self.needsDisplay = YES; }  // keeps pulsing
- (void)setIsEnabled:(BOOL)enabled { if (enabled != _isEnabled) { _isEnabled = enabled; self.needsDisplay = YES; } }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
- (void)mouseEntered:(NSEvent*)e { _hover = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent*)e { _hover = NO; _pressed = NO; self.needsDisplay = YES; }
- (void)mouseDown:(NSEvent*)e { if (_isEnabled) { _pressed = YES; self.needsDisplay = YES; } }
- (void)mouseUp:(NSEvent*)e {
    const BOOL fire = _pressed && _isEnabled && NSPointInRect([self convertPoint:e.locationInWindow fromView:nil], self.bounds);
    _pressed = NO;
    self.needsDisplay = YES;
    if (fire && _onClick) _onClick();
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    const CGFloat d = MIN(b.size.width, b.size.height) - 8, cx = b.size.width / 2, cy = b.size.height / 2;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSetAlpha(ctx, _isEnabled ? 1.0 : 0.4);
    if (_isOn) {  // pulsing halo
        const double pulse = 0.5 + 0.5 * sin(CACurrentMediaTime() * 2.0 * M_PI * kPulseHz);
        const CGFloat halo = d / 2 + 2 + 3 * pulse;
        TTRFillRounded(NSMakeRect(cx - halo, cy - halo, 2 * halo, 2 * halo), halo, TTRHex(0xFF4D5E, 0.12 + 0.22 * pulse));
    }
    const NSRect disc = NSMakeRect(cx - d / 2, cy - d / 2, d, d);
    NSColor* fill = _isOn ? TTR_RECORD : (_hover ? TTRHex(0x2F3744) : TTR_PANEL_RAISED);
    if (_pressed) fill = [fill blendedColorWithFraction:0.25 ofColor:[NSColor blackColor]];
    TTRFillRounded(disc, d / 2, fill);
    TTRStrokeRounded(disc, d / 2, _isOn ? TTRHex(0xFF9AA4) : TTR_BORDER, 1.5);
    if (_isOn) {
        const CGFloat s = d * 0.30;  // stop square
        TTRFillRounded(NSMakeRect(cx - s / 2, cy - s / 2, s, s), 3, [NSColor whiteColor]);
    } else {
        const CGFloat s = d * 0.42;
        TTRFillRounded(NSMakeRect(cx - s / 2, cy - s / 2, s, s), s / 2, TTR_RECORD);
    }
}

@end

// ============================================================ TTRInfoBox_v1

@implementation TTRInfoBox_v1

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }

- (void)setLines:(NSArray<NSString*>*)lines {
    if (![lines isEqualToArray:_lines]) { _lines = [lines copy]; self.needsDisplay = YES; }
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    TTRFillRounded(b, 7, TTR_BG);
    TTRStrokeRounded(b, 7, TTR_BORDER);
    const NSUInteger count = std::max<NSUInteger>(1, _lines.count);
    const CGFloat lineH = MIN(16.0, (b.size.height - 8) / count), top = (b.size.height - lineH * count) / 2;
    for (NSUInteger i = 0; i < _lines.count; ++i)
        TTRDrawText(_lines[i], NSMakeRect(9, top + lineH * i, b.size.width - 14, lineH), TTRMono(11, i == 0 ? NSFontWeightSemibold : NSFontWeightRegular),
                    i == 0 ? TTR_TEXT : TTR_DIM);
}

@end

// ============================================================ TTRScrollBar_v1

static const CGFloat kMinThumb = 24.0;

@implementation TTRScrollBar_v1 {
    BOOL _dragging;
    CGFloat _grabOffset;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _contentLength = 1;
        _visibleLength = 1;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (void)setContentLength:(double)v { if (v != _contentLength) { _contentLength = v; self.needsDisplay = YES; } }
- (void)setVisibleLength:(double)v { if (v != _visibleLength) { _visibleLength = v; self.needsDisplay = YES; } }
- (void)setOffset:(double)v { if (v != _offset) { _offset = v; self.needsDisplay = YES; } }

- (NSRect)thumbRect {
    const NSRect b = self.bounds;
    const double content = MAX(_contentLength, 1e-6), fraction = MIN(1.0, _visibleLength / content);
    const CGFloat w = MAX(kMinThumb, floor(b.size.width * fraction));
    const double maxOffset = MAX(1e-9, content - _visibleLength);
    const CGFloat x = (b.size.width - w) * MIN(1.0, MAX(0.0, _offset / maxOffset));
    return NSMakeRect(x, 2, w, b.size.height - 4);
}

- (void)scrollToThumbX:(CGFloat)x {
    const NSRect thumb = [self thumbRect];
    const double travel = MAX(1.0, self.bounds.size.width - thumb.size.width), maxOffset = MAX(0.0, _contentLength - _visibleLength);
    const double value = MIN(1.0, MAX(0.0, x / travel)) * maxOffset;
    _offset = value;
    self.needsDisplay = YES;
    if (_onScroll) _onScroll(value);
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    const NSRect thumb = [self thumbRect];
    _dragging = YES;
    _grabOffset = NSPointInRect(p, thumb) ? p.x - thumb.origin.x : thumb.size.width / 2;
    [self scrollToThumbX:p.x - _grabOffset];
}
- (void)mouseDragged:(NSEvent*)event {
    if (_dragging) [self scrollToThumbX:[self convertPoint:event.locationInWindow fromView:nil].x - _grabOffset];
}
- (void)mouseUp:(NSEvent*)event { _dragging = NO; }

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    TTRFillRounded(b, b.size.height / 2, TTRHex(0x0F1115));
    if (_visibleLength >= _contentLength) return;  // everything visible: only the track
    TTRFillRounded([self thumbRect], (b.size.height - 4) / 2, _dragging ? TTR_ACCENT : TTRHex(0x3A4352));
}

@end
