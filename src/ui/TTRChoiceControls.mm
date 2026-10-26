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
    TTRFillRounded(b, 7, _hover ? TTRHex(0x2F3744) : TTR_PANEL_RAISED);
    TTRStrokeRounded(b, 7, TTR_BORDER);
    TTRDrawText([_title uppercaseString], NSMakeRect(11, 0, 70, b.size.height), TTRFont(10, NSFontWeightSemibold), TTR_DIM, NSTextAlignmentLeft, 0.8);
    NSString* value = (_selectedIndex >= 0 && _selectedIndex < (NSInteger)_items.count) ? _items[_selectedIndex] : @"";
    TTRDrawText(value, NSMakeRect(78, 0, b.size.width - 78 - 26, b.size.height), TTRFont(12, NSFontWeightSemibold), TTR_TEXT, NSTextAlignmentRight);
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
    TTRFillRounded(b, 7, TTR_BG);
    TTRStrokeRounded(b, 7, _isHighlighted ? TTRHex(0x2EE6C5, 0.5) : TTR_BORDER);
    NSFont* titleFont = TTRFont(10, NSFontWeightSemibold);
    if (_isStacked) {  // title above the value, both centred
        TTRDrawText([_title uppercaseString], NSMakeRect(0, 4, b.size.width, 12), titleFont, TTR_DIM, NSTextAlignmentCenter, 0.8);
        TTRDrawText(_value ?: @"-", NSMakeRect(0, 16, b.size.width, b.size.height - 18), TTRMono(13, NSFontWeightSemibold), _isHighlighted ? TTR_ACCENT : TTR_TEXT, NSTextAlignmentCenter);
        return;
    }
    TTRDrawText([_title uppercaseString], NSMakeRect(11, 0, 40, b.size.height), titleFont, TTR_DIM, NSTextAlignmentLeft, 0.8);
    TTRDrawText(_value ?: @"-", NSMakeRect(50, 0, b.size.width - 50 - 11, b.size.height), TTRMono(13, NSFontWeightSemibold), _isHighlighted ? TTR_ACCENT : TTR_TEXT, NSTextAlignmentRight);
}

@end
