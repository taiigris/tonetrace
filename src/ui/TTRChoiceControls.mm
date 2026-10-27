#import "ui/TTRControls.h"

#import "ui/TTRTheme.h"

// ============================================================ TTRDropdown_v1

@implementation TTRDropdown_v1 {
    BOOL _hover;
    NSTrackingArea* _tracking;
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
- (void)setSelectedIndex:(NSInteger)i { if (i != _selectedIndex) { _selectedIndex = i; self.needsDisplay = YES; } }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
- (void)mouseEntered:(NSEvent*)e { _hover = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent*)e { _hover = NO; self.needsDisplay = YES; }

- (void)mouseDown:(NSEvent*)event {
    NSMenu* menu = [[NSMenu alloc] init];
    for (NSUInteger i = 0; i < _items.count; ++i) {
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:_items[i] action:@selector(pickItem:) keyEquivalent:@""];
        item.target = self;
        item.tag = (NSInteger)i;
        item.state = (NSInteger)i == _selectedIndex ? NSControlStateValueOn : NSControlStateValueOff;
        [menu addItem:item];
    }
    [menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0, self.bounds.size.height + 3) inView:self];
}

- (void)pickItem:(NSMenuItem*)item {
    self.selectedIndex = item.tag;
    if (_onChange) _onChange(item.tag);
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    TTRFillRounded(b, kTTRRadius, _hover ? TTR_HOVER : TTR_PANEL_RAISED);
    TTRStrokeRounded(b, kTTRRadius, TTR_BORDER);
    TTRDrawText([_title uppercaseString], NSMakeRect(10, 0, 40, b.size.height), TTRFont(11, NSFontWeightSemibold), TTR_DIM, NSTextAlignmentLeft, 0.6);
    NSString* value = (_selectedIndex >= 0 && _selectedIndex < (NSInteger)_items.count) ? _items[_selectedIndex] : @"";
    TTRDrawText(value, NSMakeRect(50, 0, b.size.width - 50 - 26, b.size.height), TTRFont(12, NSFontWeightSemibold), TTR_TEXT, NSTextAlignmentRight);
    NSBezierPath* chev = [NSBezierPath bezierPath];
    const CGFloat cx = b.size.width - 14, cy = b.size.height / 2;
    [chev moveToPoint:NSMakePoint(cx - 3.5, cy - 1.5)];
    [chev lineToPoint:NSMakePoint(cx, cy + 2)];
    [chev lineToPoint:NSMakePoint(cx + 3.5, cy - 1.5)];
    chev.lineWidth = 1.5;
    chev.lineCapStyle = NSLineCapStyleRound;
    chev.lineJoinStyle = NSLineJoinStyleRound;
    [TTR_DIM setStroke];
    [chev stroke];
}

@end

// ============================================================ TTRReadout_v1

@implementation TTRReadout_v1

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}
- (BOOL)isFlipped { return YES; }
- (void)setValue:(NSString*)value { if (![value isEqualToString:_value]) { _value = [value copy]; self.needsDisplay = YES; } }
- (void)setIsHighlighted:(BOOL)h { if (h != _isHighlighted) { _isHighlighted = h; self.needsDisplay = YES; } }

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    TTRFillRounded(b, kTTRRadius, TTR_BG);
    TTRStrokeRounded(b, kTTRRadius, _isHighlighted ? TTRHex(0x2EE6C5, 0.5) : TTR_BORDER);
    NSFont* titleFont = TTRFont(11, NSFontWeightSemibold);
    if (_isStacked) {  // title above the value, both centred
        TTRDrawText([_title uppercaseString], NSMakeRect(0, 4, b.size.width, 12), titleFont, TTR_DIM, NSTextAlignmentCenter, 0.6);
        TTRDrawText(_value ?: @"-", NSMakeRect(0, 16, b.size.width, b.size.height - 18), TTRMono(13, NSFontWeightSemibold), _isHighlighted ? TTR_ACCENT : TTR_TEXT, NSTextAlignmentCenter);
        return;
    }
    const CGFloat titleW = TTRTextWidth([_title uppercaseString], titleFont, 0.6), valueX = 11 + MAX(40.0, titleW + 6);
    TTRDrawText([_title uppercaseString], NSMakeRect(11, 0, titleW + 2, b.size.height), titleFont, TTR_DIM, NSTextAlignmentLeft, 0.6);
    NSFont* valueFont = TTRMono(13, NSFontWeightSemibold);
    for (CGFloat size = 12; size >= 10 && TTRTextWidth(_value ?: @"-", valueFont) > b.size.width - valueX - 8; size -= 1) valueFont = TTRMono(size, NSFontWeightSemibold);
    TTRDrawText(_value ?: @"-", NSMakeRect(valueX, 0, b.size.width - valueX - 11, b.size.height), valueFont, _isHighlighted ? TTR_ACCENT : TTR_TEXT, NSTextAlignmentRight);
}

@end

// ============================================================ TTRSlider_v1

static const CGFloat kSliderTrackY = 26.0, kSliderThumb = 12.0;

@implementation TTRSlider_v1 {
    BOOL _dragging;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
        _value = 0.0;
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

- (void)setFromX:(CGFloat)x {
    const CGFloat span = MAX(1.0, self.bounds.size.width - kSliderThumb);
    self.value = (x - kSliderThumb / 2) / span;
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
    [self setFromX:[self convertPoint:event.locationInWindow fromView:nil].x];
}
- (void)mouseDragged:(NSEvent*)event { if (_dragging) [self setFromX:[self convertPoint:event.locationInWindow fromView:nil].x]; }
- (void)mouseUp:(NSEvent*)event {
    if (_dragging && _onEndEdit) _onEndEdit();
    _dragging = NO;
    self.needsDisplay = YES;
}
- (void)scrollWheel:(NSEvent*)event {
    if (_onBeginEdit) _onBeginEdit();
    self.value = _value + event.scrollingDeltaY * 0.004;
    if (_onChange) _onChange(_value);
    if (_onEndEdit) _onEndEdit();
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    TTRDrawText([_title uppercaseString], NSMakeRect(0, 0, b.size.width - 34, 16), TTRFont(11, NSFontWeightSemibold), TTR_DIM, NSTextAlignmentLeft, 0.3);
    NSString* text = _valueFormatter ? _valueFormatter(_value) : [NSString stringWithFormat:@"%.0f%%", _value * 100];
    TTRDrawText(text, NSMakeRect(b.size.width - 40, 0, 40, 16), TTRMono(13), TTR_TEXT, NSTextAlignmentRight);
    const CGFloat span = b.size.width - kSliderThumb, x = kSliderThumb / 2 + span * _value;
    TTRFillRounded(NSMakeRect(0, kSliderTrackY - 1, b.size.width, 2), 1, TTR_BORDER);
    if (_value > 0.0) TTRFillRounded(NSMakeRect(0, kSliderTrackY - 1, x, 2), 1, TTR_ACCENT);
    const NSRect thumb = NSMakeRect(x - kSliderThumb / 2, kSliderTrackY - kSliderThumb / 2, kSliderThumb, kSliderThumb);
    TTRFillRounded(thumb, kSliderThumb / 2, _dragging ? TTR_ACCENT : TTR_TEXT);
}

@end

// ============================================================ TTRPopoverView_v1

@implementation TTRPopoverView_v1
- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return NO; }
- (void)drawRect:(NSRect)dirty {
    TTRFillRounded(self.bounds, 8, TTR_PANEL);
    TTRStrokeRounded(self.bounds, 8, TTR_BORDER);
}
- (void)mouseDown:(NSEvent*)event {}  // clicks on the panel itself do not reach the map behind it
@end
