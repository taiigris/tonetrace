// Record button, crosshair info box and scroll bar.
#import <QuartzCore/QuartzCore.h>

#include <algorithm>

#import "ui/TTRControls.h"
#import "ui/TTRTheme.h"

// ============================================================ TTRRecordButton_v1


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
    const CGFloat labelH = 16, d = MIN(b.size.width, b.size.height - labelH) - 4, cx = b.size.width / 2, cy = (b.size.height - labelH) / 2;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSetAlpha(ctx, _isEnabled ? 1.0 : 0.4);
    const NSRect disc = NSMakeRect(cx - d / 2, cy - d / 2, d, d);
    NSColor* fill = _isOn ? TTR_RECORD : (_hover ? TTR_HOVER : TTR_PANEL_RAISED);  // red only while recording
    if (_pressed) fill = [fill blendedColorWithFraction:0.2 ofColor:[NSColor blackColor]];
    TTRFillRounded(disc, d / 2, fill);
    TTRStrokeRounded(disc, d / 2, _isOn ? TTR_RECORD : TTR_BORDER, 1.0);
    if (_isOn) {
        const CGFloat s = d * 0.30;  // stop square
        TTRFillRounded(NSMakeRect(cx - s / 2, cy - s / 2, s, s), 2, [NSColor whiteColor]);
    } else {
        const CGFloat s = d * 0.36;
        TTRFillRounded(NSMakeRect(cx - s / 2, cy - s / 2, s, s), s / 2, TTR_TEXT);
    }
    TTRDrawText(@"RECORD", NSMakeRect(0, b.size.height - labelH, b.size.width, labelH), TTRFont(11, NSFontWeightSemibold), _isOn ? TTR_TEXT : TTR_DIM, NSTextAlignmentCenter, 0.6);
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
    TTRFillRounded(b, kTTRRadius, TTR_BG);
    TTRStrokeRounded(b, kTTRRadius, TTR_BORDER);
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
    TTRFillRounded(b, b.size.height / 2, TTR_BG);
    if (_visibleLength >= _contentLength) return;  // everything visible: only the track
    TTRFillRounded([self thumbRect], (b.size.height - 4) / 2, _dragging ? TTR_ACCENT : TTRHex(0x3A3E44));
}

@end
