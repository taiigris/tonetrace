#import "ui/TTRControls.h"

#import "ui/TTRTheme.h"

// ============================================================ TTRKnob_v1

static const double kKnobStartDeg = 135.0, kKnobSweepDeg = 270.0;
static const CGFloat kKnobDiameter = 34.0;

@implementation TTRKnob_v1 {
    BOOL _dragging;
    NSPoint _lastPoint;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
        _value = 0.5;
        _defaultValue = 0.5;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isDragging { return _dragging; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (void)setValue:(double)value {
    value = MIN(1.0, MAX(0.0, value));
    if (value != _value) { _value = value; self.needsDisplay = YES; }
}

- (void)userSetValue:(double)v {
    self.value = v;
    if (_onChange) _onChange(_value);
}

- (void)beginEdit { if (_onBeginEdit) _onBeginEdit(); }
- (void)endEdit { if (_onEndEdit) _onEndEdit(); }

- (void)mouseDown:(NSEvent*)event {
    if (event.clickCount == 2) {
        [self beginEdit];
        [self userSetValue:_defaultValue];
        [self endEdit];
        return;
    }
    _dragging = YES;
    _lastPoint = [self convertPoint:event.locationInWindow fromView:nil];
    [self beginEdit];
}
- (void)mouseDragged:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    const double fine = (event.modifierFlags & NSEventModifierFlagShift) ? 0.2 : 1.0;
    [self userSetValue:_value + (_lastPoint.y - p.y) * 0.006 * fine];
    _lastPoint = p;
    self.needsDisplay = YES;
}
- (void)mouseUp:(NSEvent*)event {
    if (_dragging) [self endEdit];
    _dragging = NO;
    self.needsDisplay = YES;
}
- (void)scrollWheel:(NSEvent*)event {
    [self beginEdit];
    [self userSetValue:_value + event.scrollingDeltaY * 0.004];
    [self endEdit];
}

- (NSBezierPath*)arcFrom:(double)fromFraction to:(double)toFraction center:(NSPoint)c radius:(CGFloat)r {
    NSBezierPath* path = [NSBezierPath bezierPath];
    const int steps = 48;
    for (int i = 0; i <= steps; ++i) {
        const double f = fromFraction + (toFraction - fromFraction) * i / steps;
        const double a = (kKnobStartDeg + kKnobSweepDeg * f) * M_PI / 180.0;
        const NSPoint pt = NSMakePoint(c.x + r * cos(a), c.y + r * sin(a));
        i == 0 ? [path moveToPoint:pt] : [path lineToPoint:pt];
    }
    path.lineWidth = 2.5;
    path.lineCapStyle = NSLineCapStyleRound;
    return path;
}

- (void)drawDialAt:(NSPoint)c radius:(CGFloat)r {
    [TTR_BORDER setStroke];
    [[self arcFrom:0 to:1 center:c radius:r] stroke];
    if (_value > 0.001) {
        [TTR_ACCENT setStroke];
        [[self arcFrom:0 to:_value center:c radius:r] stroke];
    }
    const CGFloat cap = r * 0.55;
    TTRFillRounded(NSMakeRect(c.x - cap, c.y - cap, 2 * cap, 2 * cap), cap, TTR_PANEL_RAISED);
    TTRStrokeRounded(NSMakeRect(c.x - cap, c.y - cap, 2 * cap, 2 * cap), cap, TTR_BORDER);
    const double a = (kKnobStartDeg + kKnobSweepDeg * _value) * M_PI / 180.0;
    NSBezierPath* tick = [NSBezierPath bezierPath];
    [tick moveToPoint:NSMakePoint(c.x + cap * 0.35 * cos(a), c.y + cap * 0.35 * sin(a))];
    [tick lineToPoint:NSMakePoint(c.x + cap * 0.9 * cos(a), c.y + cap * 0.9 * sin(a))];
    tick.lineWidth = 1.5;
    tick.lineCapStyle = NSLineCapStyleRound;
    [TTR_TEXT setStroke];
    [tick stroke];
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    NSString* valueText = _valueFormatter ? _valueFormatter(_value) : [NSString stringWithFormat:@"%.0f%%", _value * 100];
    if (_isCompact) {  // dial on top, title and value centred below
        const CGFloat textH = 30, dialArea = b.size.height - textH, r = MIN(16.0, dialArea / 2 - 2);
        [self drawDialAt:NSMakePoint(b.size.width / 2, dialArea / 2 + 1) radius:r];
        TTRDrawText([_title uppercaseString], NSMakeRect(0, b.size.height - textH, b.size.width, 14), TTRFont(11, NSFontWeightSemibold), TTR_DIM, NSTextAlignmentCenter, 0.6);
        TTRDrawText(valueText, NSMakeRect(0, b.size.height - 16, b.size.width, 16), TTRMono(13), TTR_TEXT, NSTextAlignmentCenter);
        return;
    }
    const NSPoint c = NSMakePoint(kKnobDiameter / 2 + 2, b.size.height / 2);
    [self drawDialAt:c radius:kKnobDiameter / 2 - 3];
    const CGFloat tx = kKnobDiameter + 12;
    TTRDrawText([_title uppercaseString], NSMakeRect(tx, b.size.height / 2 - 16, b.size.width - tx, 14), TTRFont(10, NSFontWeightSemibold), TTR_DIM, NSTextAlignmentLeft, 0.8);
    TTRDrawText(valueText, NSMakeRect(tx, b.size.height / 2 + 1, b.size.width - tx, 16), TTRMono(13), TTR_TEXT);
}

@end
