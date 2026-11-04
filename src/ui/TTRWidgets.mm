#import "ui/TTRWidgets.h"

#include <algorithm>
#include <cmath>

#import "ui/TTRTheme.h"

static const CGFloat kDragThreshold = 5.0;
static const uint32_t kWarning = 0xE08A2E;

// ============================================================ TTRGlyphButton_v1

@implementation TTRGlyphButton_v1 {
    BOOL _hover, _pressed;
    NSTrackingArea* _tracking;
}

- (instancetype)initWithGlyph:(TTRTransportGlyph_v1)glyph title:(NSString*)title {
    self = [super initWithFrame:NSMakeRect(0, 0, 40, 40)];
    if (self) {
        _glyph = glyph;
        _title = [title copy];
        _isEnabled = YES;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)setIsOn:(BOOL)isOn { if (_isOn != isOn) { _isOn = isOn; self.needsDisplay = YES; } else if (isOn && _glyph == TTRTransportRecord) self.needsDisplay = YES; }
- (void)setIsEnabled:(BOOL)isEnabled { if (_isEnabled != isEnabled) { _isEnabled = isEnabled; self.needsDisplay = YES; } }
- (void)setTitle:(NSString*)title { if (![_title isEqualToString:title]) { _title = [title copy]; self.needsDisplay = YES; } }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                                               owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
- (void)mouseEntered:(NSEvent*)e { _hover = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent*)e { _hover = NO; _pressed = NO; self.needsDisplay = YES; }
- (void)mouseDown:(NSEvent*)event { if (_isEnabled) { _pressed = YES; self.needsDisplay = YES; } }
- (void)mouseUp:(NSEvent*)event {
    const BOOL fire = _pressed && _isEnabled && NSPointInRect([self convertPoint:event.locationInWindow fromView:nil], self.bounds);
    _pressed = NO;
    self.needsDisplay = YES;
    if (fire && _onClick) _onClick();
}

- (void)drawGlyph:(NSRect)g color:(NSColor*)color {
    const NSPoint c = NSMakePoint(NSMidX(g), NSMidY(g));
    [color setFill];
    [color setStroke];
    NSBezierPath* p = [NSBezierPath bezierPath];
    p.lineWidth = 1.6;
    p.lineCapStyle = NSLineCapStyleRound;
    p.lineJoinStyle = NSLineJoinStyleRound;
    switch (_glyph) {
        case TTRTransportRewind:
            [[NSBezierPath bezierPathWithRect:NSMakeRect(c.x - 6, c.y - 6, 2, 12)] fill];
            [p moveToPoint:NSMakePoint(c.x + 6, c.y - 6)];
            [p lineToPoint:NSMakePoint(c.x - 3, c.y)];
            [p lineToPoint:NSMakePoint(c.x + 6, c.y + 6)];
            [p closePath];
            [p fill];
            break;
        case TTRTransportPlay:
            if (_isOn) {
                [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(c.x - 6, c.y - 7, 4, 14) xRadius:1 yRadius:1] fill];
                [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(c.x + 2, c.y - 7, 4, 14) xRadius:1 yRadius:1] fill];
            } else {
                [p moveToPoint:NSMakePoint(c.x - 5, c.y - 7)];
                [p lineToPoint:NSMakePoint(c.x + 7, c.y)];
                [p lineToPoint:NSMakePoint(c.x - 5, c.y + 7)];
                [p closePath];
                [p fill];
            }
            break;
        case TTRTransportRecord: {
            const double pulse = _isOn ? 0.55 + 0.45 * (0.5 + 0.5 * sin(CACurrentMediaTime() * 5.0)) : 1.0;
            [(_isOn ? TTR_RECORD : TTRHex(0x6B2430)) setFill];
            CGContextSetAlpha([NSGraphicsContext currentContext].CGContext, CGFloat(pulse));
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 6, c.y - 6, 12, 12)] fill];
            CGContextSetAlpha([NSGraphicsContext currentContext].CGContext, 1.0);
            break;
        }
        case TTRTransportFollow:
            [p moveToPoint:NSMakePoint(c.x - 7, c.y - 7)]; [p lineToPoint:NSMakePoint(c.x - 7, c.y + 7)];
            [p moveToPoint:NSMakePoint(c.x + 7, c.y - 7)]; [p lineToPoint:NSMakePoint(c.x + 7, c.y + 7)];
            [p moveToPoint:NSMakePoint(c.x, c.y - 5)]; [p lineToPoint:NSMakePoint(c.x, c.y + 6)];
            [p moveToPoint:NSMakePoint(c.x - 3, c.y - 5)]; [p lineToPoint:NSMakePoint(c.x + 3, c.y - 5)];
            [p stroke];
            break;
        case TTRTransportTrash:
            [p moveToPoint:NSMakePoint(c.x - 6, c.y - 4)]; [p lineToPoint:NSMakePoint(c.x + 6, c.y - 4)];
            [p moveToPoint:NSMakePoint(c.x - 2, c.y - 6)]; [p lineToPoint:NSMakePoint(c.x + 2, c.y - 6)];
            [p moveToPoint:NSMakePoint(c.x - 5, c.y - 3)]; [p lineToPoint:NSMakePoint(c.x - 4, c.y + 7)];
            [p lineToPoint:NSMakePoint(c.x + 4, c.y + 7)]; [p lineToPoint:NSMakePoint(c.x + 5, c.y - 3)];
            [p stroke];
            break;
        case TTRTransportOptions:
            for (int i = 0; i < 3; ++i) {
                const CGFloat y = c.y - 5 + i * 5, knob = c.x + (i == 1 ? 3 : -3);
                [p moveToPoint:NSMakePoint(c.x - 7, y)];
                [p lineToPoint:NSMakePoint(c.x + 7, y)];
                [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(knob - 2, y - 2, 4, 4)] fill];
            }
            [p stroke];
            break;
        case TTRTransportSpeaker:
            [p moveToPoint:NSMakePoint(c.x - 7, c.y - 2)]; [p lineToPoint:NSMakePoint(c.x - 4, c.y - 2)]; [p lineToPoint:NSMakePoint(c.x, c.y - 6)];
            [p lineToPoint:NSMakePoint(c.x, c.y + 6)]; [p lineToPoint:NSMakePoint(c.x - 4, c.y + 2)]; [p lineToPoint:NSMakePoint(c.x - 7, c.y + 2)];
            [p closePath];
            [p fill];
            if (_isOn) {
                NSBezierPath* x = [NSBezierPath bezierPath];
                x.lineWidth = 1.6;
                [x moveToPoint:NSMakePoint(c.x + 3, c.y - 3)]; [x lineToPoint:NSMakePoint(c.x + 8, c.y + 3)];
                [x moveToPoint:NSMakePoint(c.x + 8, c.y - 3)]; [x lineToPoint:NSMakePoint(c.x + 3, c.y + 3)];
                [x stroke];
            } else {
                NSBezierPath* wave = [NSBezierPath bezierPath];
                wave.lineWidth = 1.4;
                [wave appendBezierPathWithArcWithCenter:NSMakePoint(c.x + 1, c.y) radius:5 startAngle:-45 endAngle:45];
                [wave stroke];
            }
            break;
        case TTRTransportFolder:
            [p moveToPoint:NSMakePoint(c.x - 7, c.y - 5)]; [p lineToPoint:NSMakePoint(c.x - 2, c.y - 5)]; [p lineToPoint:NSMakePoint(c.x, c.y - 3)];
            [p lineToPoint:NSMakePoint(c.x + 7, c.y - 3)]; [p lineToPoint:NSMakePoint(c.x + 7, c.y + 6)]; [p lineToPoint:NSMakePoint(c.x - 7, c.y + 6)];
            [p closePath];
            [p stroke];
            break;
        case TTRTransportDownload:
            [p moveToPoint:NSMakePoint(c.x, c.y - 7)]; [p lineToPoint:NSMakePoint(c.x, c.y + 3)];
            [p moveToPoint:NSMakePoint(c.x - 4, c.y - 1)]; [p lineToPoint:NSMakePoint(c.x, c.y + 3)]; [p lineToPoint:NSMakePoint(c.x + 4, c.y - 1)];
            [p moveToPoint:NSMakePoint(c.x - 7, c.y + 7)]; [p lineToPoint:NSMakePoint(c.x + 7, c.y + 7)];
            [p stroke];
            break;
    }
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSetAlpha(ctx, _isEnabled ? 1.0 : 0.38);
    NSColor* fill = _hover ? TTR_HOVER : TTR_PANEL_RAISED;
    NSColor* fg = _hover ? TTR_TEXT : TTRHex(0xC9CDD3);
    if (_isOn && _glyph != TTRTransportRecord && _glyph != TTRTransportPlay) { fill = TTR_ACCENT_TINT; fg = TTR_ACCENT; }
    if (_isOn && _isWarning) { fill = TTRHex(kWarning, 0.28); fg = TTRHex(0xFFB35C); }
    if (_pressed) fill = [fill blendedColorWithFraction:0.2 ofColor:[NSColor blackColor]];
    TTRFillRounded(b, kTTRRadius, fill);
    if (_isAccent) TTRStrokeRounded(b, kTTRRadius, TTRHex(0x5B8CFF), 1.5);
    if (_isAccent) fg = TTRHex(0x9DB8FF);
    if (_title.length == 0) {
        [self drawGlyph:b color:fg];
        return;
    }
    NSFont* font = TTRFont(13, NSFontWeightSemibold);
    const CGFloat textW = TTRTextWidth(_title, font, 0.4), groupW = 18 + 8 + textW;
    const CGFloat x0 = floor((b.size.width - groupW) / 2.0);
    [self drawGlyph:NSMakeRect(x0, 0, 18, b.size.height) color:fg];
    TTRDrawText(_title, NSMakeRect(x0 + 26, 0, textW + 2, b.size.height), font, fg, NSTextAlignmentLeft, 0.4);
}

@end

// ============================================================ TTRGlyphDragButton_v1

@interface TTRGlyphDragButton_v1 () <NSDraggingSource>
@end

@implementation TTRGlyphDragButton_v1 {
    NSPoint _downPoint;
    BOOL _armed;
}

- (void)mouseDown:(NSEvent*)event {
    _downPoint = [self convertPoint:event.locationInWindow fromView:nil];
    _armed = YES;
    [super mouseDown:event];
}

- (void)mouseDragged:(NSEvent*)event {
    if (!_armed || !self.isEnabled) return;
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (hypot(p.x - _downPoint.x, p.y - _downPoint.y) < kDragThreshold) return;
    _armed = NO;
    [self mouseExited:event];
    NSURL* url = self.dragFileProvider ? self.dragFileProvider() : nil;
    if (!url) return;
    NSDraggingItem* item = [[NSDraggingItem alloc] initWithPasteboardWriter:url];
    NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:url.path];
    icon.size = NSMakeSize(32, 32);
    [item setDraggingFrame:NSMakeRect(p.x - 16, p.y - 16, 32, 32) contents:icon];
    [self beginDraggingSessionWithItems:@[ item ] event:event source:self];
}

- (void)mouseUp:(NSEvent*)event {
    _armed = NO;
    [super mouseUp:event];
}

- (NSDragOperation)draggingSession:(NSDraggingSession*)session sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
    return NSDragOperationCopy;
}

- (BOOL)ignoreModifierKeysForDraggingSession:(NSDraggingSession*)session { return YES; }

@end

// ============================================================ TTRLineSlider_v1

@implementation TTRLineSlider_v1 {
    BOOL _dragging;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (BOOL)isDragging { return _dragging; }
- (void)setValue:(double)value {
    const double v = std::clamp(std::isfinite(value) ? value : 0.0, 0.0, 1.0);
    if (v != _value) { _value = v; self.needsDisplay = YES; }
}

- (CGFloat)labelWidth:(NSString*)label { return label.length ? TTRTextWidth(label, TTRFont(11, NSFontWeightSemibold), 1.0) + 12 : 0; }

- (NSRect)trackRect {
    const NSRect b = self.bounds;
    const CGFloat left = [self labelWidth:_leftLabel], right = _valueText ? 44 : [self labelWidth:_rightLabel];
    return NSMakeRect(left + 6, floor(NSMidY(b)) - 2, MAX(10, b.size.width - left - right - 12), 4);
}

- (void)setFromPoint:(NSPoint)p {
    const NSRect t = [self trackRect];
    self.value = (p.x - t.origin.x) / t.size.width;
    if (_onChange) _onChange(_value);
}

- (void)mouseDown:(NSEvent*)event {
    if (event.clickCount == 2) {
        if (_onBeginEdit) _onBeginEdit();
        self.value = _defaultValue;
        if (_onChange) _onChange(_value);
        if (_onEndEdit) _onEndEdit();
        return;
    }
    _dragging = YES;
    if (_onBeginEdit) _onBeginEdit();
    [self setFromPoint:[self convertPoint:event.locationInWindow fromView:nil]];
}
- (void)mouseDragged:(NSEvent*)event { if (_dragging) [self setFromPoint:[self convertPoint:event.locationInWindow fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event {
    if (!_dragging) return;
    _dragging = NO;
    if (_onEndEdit) _onEndEdit();
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds, t = [self trackRect];
    NSFont* labelFont = TTRFont(11, NSFontWeightSemibold);
    if (_leftLabel.length) TTRDrawText(_leftLabel, NSMakeRect(0, 0, [self labelWidth:_leftLabel], b.size.height), labelFont, TTR_DIM, NSTextAlignmentRight, 1.0);
    if (_rightLabel.length && !_valueText)
        TTRDrawText(_rightLabel, NSMakeRect(NSMaxX(t) + 6, 0, b.size.width - NSMaxX(t) - 6, b.size.height), labelFont, TTR_DIM, NSTextAlignmentLeft, 1.0);
    if (_valueText)
        TTRDrawText(_valueText(_value), NSMakeRect(NSMaxX(t) + 6, 0, b.size.width - NSMaxX(t) - 6, b.size.height), TTRMono(11), TTR_DIM, NSTextAlignmentRight);
    TTRFillRounded(t, 2, TTRHex(0x2E3238));
    const uint32_t color = _trackColor ? _trackColor : 0x5B8CFF;
    const CGFloat x = t.origin.x + CGFloat(_value) * t.size.width;
    TTRFillRounded(NSMakeRect(t.origin.x, t.origin.y, x - t.origin.x, t.size.height), 2, TTRHex(color));
    TTRFillRounded(NSMakeRect(round(x) - 2, NSMidY(t) - 8, 4, 16), 1.5, TTRHex(0xF2F4F7));
}

@end

// ============================================================ TTRStepper_v1

@implementation TTRStepper_v1 {
    BOOL _dragging;
    CGFloat _dragY;
    double _dragStart;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        _minValue = 20;
        _maxValue = 400;
        _step = 1;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)setValue:(double)value { if (value != _value) { _value = value; self.needsDisplay = YES; } }

- (void)commit:(double)v {
    const double clamped = std::clamp(std::round(v / _step) * _step, _minValue, _maxValue);
    if (clamped == _value) return;
    self.value = clamped;
    if (_onChange) _onChange(clamped);
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (p.x > self.bounds.size.width - 22) {  // the arrows
        [self commit:_value + (p.y < NSMidY(self.bounds) ? _step : -_step)];
        return;
    }
    _dragging = YES;
    _dragY = p.y;
    _dragStart = _value;
}
- (void)mouseDragged:(NSEvent*)event {
    if (!_dragging) return;
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    [self commit:_dragStart + std::round((_dragY - p.y) / 4.0) * _step];
}
- (void)mouseUp:(NSEvent*)event { _dragging = NO; }
- (void)scrollWheel:(NSEvent*)event {
    if (event.scrollingDeltaY != 0) [self commit:_value + (event.scrollingDeltaY > 0 ? _step : -_step)];
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    TTRFillRounded(b, kTTRRadius, TTR_PANEL_RAISED);
    NSFont* title = TTRFont(11, NSFontWeightSemibold);
    const CGFloat titleW = TTRTextWidth(_title, title, 1.2);
    TTRDrawText(_title, NSMakeRect(14, 0, titleW + 2, b.size.height), title, TTR_DIM, NSTextAlignmentLeft, 1.2);
    NSString* text = std::fabs(_value - std::round(_value)) < 0.05 ? [NSString stringWithFormat:@"%.0f", _value] : [NSString stringWithFormat:@"%.1f", _value];
    const CGFloat valueX = 14 + titleW + 10;
    TTRDrawText(text, NSMakeRect(valueX, 0, MAX(0.0, b.size.width - valueX - 26), b.size.height), TTRMono(14, NSFontWeightMedium), TTR_TEXT);
    [TTR_DIM setFill];
    const CGFloat ax = b.size.width - 16, ay = NSMidY(b);
    NSBezierPath* up = [NSBezierPath bezierPath];
    [up moveToPoint:NSMakePoint(ax - 4, ay - 2)]; [up lineToPoint:NSMakePoint(ax, ay - 7)]; [up lineToPoint:NSMakePoint(ax + 4, ay - 2)]; [up closePath];
    [up fill];
    NSBezierPath* down = [NSBezierPath bezierPath];
    [down moveToPoint:NSMakePoint(ax - 4, ay + 2)]; [down lineToPoint:NSMakePoint(ax, ay + 7)]; [down lineToPoint:NSMakePoint(ax + 4, ay + 2)]; [down closePath];
    [down fill];
}

@end

// ============================================================ TTRMeter_v1

@implementation TTRMeter_v1 {
    float _shown;
}

- (BOOL)isFlipped { return YES; }

- (void)setLevel:(float)level {
    const float target = std::isfinite(level) ? std::max(0.0f, level) : 0.0f;
    const float next = target > _shown ? target : _shown * 0.85f;  // fast attack, smooth fall
    if (std::fabs(next - _shown) > 1e-4f || (next == 0.0f) != (_shown == 0.0f)) { _shown = next < 1e-5f ? 0.0f : next; self.needsDisplay = YES; }
    _level = target;
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    const int segments = std::max(8, int(b.size.width / 9.0));
    const CGFloat gap = 3, w = (b.size.width - gap * (segments - 1)) / segments;
    const float db = _shown > 1e-5f ? 20.0f * std::log10(_shown) : -96.0f;
    const float lit = std::clamp((db + 48.0f) / 54.0f, 0.0f, 1.0f) * float(segments);  // -48 .. +6 dB
    for (int i = 0; i < segments; ++i) {
        const float pos = float(i) / float(segments - 1);
        const uint32_t color = pos > 0.9f ? 0xE5484D : pos > 0.75f ? 0xE5C14D : 0x5FBF6A;
        TTRFillRounded(NSMakeRect(i * (w + gap), 0, w, b.size.height), 1, TTRHex(float(i) < lit ? color : 0x2B2E33));
    }
}

@end

// ============================================================ TTRPill_v1

@implementation TTRPill_v1

- (BOOL)isFlipped { return YES; }
- (void)setText:(NSString*)text { if (![_text isEqualToString:text]) { _text = [text copy]; self.needsDisplay = YES; } }

- (void)drawRect:(NSRect)dirty {
    TTRFillRounded(self.bounds, kTTRRadius, TTR_PANEL_RAISED);
    NSString* text = _text ?: @"";
    NSFont* font = TTRFont(12, NSFontWeightSemibold);
    CGFloat kern = 1.0;
    if (TTRTextWidth(text, font, kern) > self.bounds.size.width - 16) { font = TTRFont(11, NSFontWeightSemibold); kern = 0.4; }
    TTRDrawText(text, NSInsetRect(self.bounds, 8, 0), font, TTRHex(_textColor ? _textColor : 0xC9CDD3), NSTextAlignmentCenter, kern);
}

@end

void TTRDrawChip(NSRect rect, NSString* text, uint32_t rgb, BOOL dim) {
    TTRFillRounded(rect, 5, TTRHex(rgb, dim ? 0.06 : 0.14));
    TTRStrokeRounded(rect, 5, TTRHex(rgb, dim ? 0.25 : 0.55), 1.0);
    TTRDrawText(text, rect, TTRMono(11, NSFontWeightBold), TTRHex(rgb, dim ? 0.45 : 1.0), NSTextAlignmentCenter);
}
