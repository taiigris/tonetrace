#import "ui/TTRControls.h"

#import "ui/TTRTheme.h"

static const CGFloat kDragThreshold = 5.0;

// ============================================================ TTRButton_v1

@implementation TTRButton_v1 {
    BOOL _hover, _pressed;
    NSTrackingArea* _tracking;
}

- (instancetype)initWithTitle:(NSString*)title kind:(TTRButtonKind_v1)kind {
    self = [super initWithFrame:NSMakeRect(0, 0, 90, 30)];
    if (self) {
        _title = [title copy];
        _kind = kind;
        _isEnabled = YES;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (void)setTitle:(NSString*)title { _title = [title copy]; self.needsDisplay = YES; }
- (void)setIsOn:(BOOL)isOn { if (_isOn != isOn) { _isOn = isOn; self.needsDisplay = YES; } }
- (void)setIsEnabled:(BOOL)isEnabled { if (_isEnabled != isEnabled) { _isEnabled = isEnabled; self.needsDisplay = YES; } }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds
                                             options:NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                                               owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}
- (void)mouseEntered:(NSEvent*)e { _hover = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent*)e { _hover = NO; _pressed = NO; self.needsDisplay = YES; }

- (void)mouseDown:(NSEvent*)event {
    if (!_isEnabled) return;
    _pressed = YES;
    self.needsDisplay = YES;
}
- (void)mouseUp:(NSEvent*)event {
    const BOOL fire = _pressed && _isEnabled && NSPointInRect([self convertPoint:event.locationInWindow fromView:nil], self.bounds);
    _pressed = NO;
    self.needsDisplay = YES;
    if (fire && _onClick) _onClick();
}

- (void)drawGlyphInRect:(NSRect)g color:(NSColor*)color {
    [color setFill];
    switch (_kind) {
        case TTRButtonKindPlay:
            if (_isOn) {
                [[NSBezierPath bezierPathWithRoundedRect:NSInsetRect(g, 2, 2) xRadius:1 yRadius:1] fill];
            } else {
                NSBezierPath* tri = [NSBezierPath bezierPath];
                [tri moveToPoint:NSMakePoint(NSMinX(g) + 2, NSMinY(g) + 1)];
                [tri lineToPoint:NSMakePoint(NSMaxX(g) - 1, NSMidY(g))];
                [tri lineToPoint:NSMakePoint(NSMinX(g) + 2, NSMaxY(g) - 1)];
                [tri closePath];
                [tri fill];
            }
            break;
        case TTRButtonKindDrag:
            for (int row = 0; row < 3; ++row)
                for (int col = 0; col < 2; ++col)
                    [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(NSMinX(g) + 2 + col * 5, NSMinY(g) + 1.5 + row * 4, 2.4, 2.4)] fill];
            break;
        default: break;
    }
}

// Header icons: thin strokes in one colour, 16 pt, centred.
- (void)drawIconInRect:(NSRect)r color:(NSColor*)color {
    const NSPoint c = NSMakePoint(NSMidX(r), NSMidY(r));
    [color setStroke];
    [color setFill];
    NSBezierPath* p = [NSBezierPath bezierPath];
    p.lineWidth = 1.5;
    p.lineCapStyle = NSLineCapStyleRound;
    switch (_glyph) {
        case TTRIconHelp:
            TTRDrawText(@"?", r, TTRFont(15, NSFontWeightSemibold), color, NSTextAlignmentCenter);
            return;
        case TTRIconInfo: {
            NSBezierPath* ring = [NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 7.5, c.y - 7.5, 15, 15)];
            ring.lineWidth = 1.5;
            [ring stroke];
            [p moveToPoint:NSMakePoint(c.x, c.y - 0.5)];
            [p lineToPoint:NSMakePoint(c.x, c.y + 3.5)];
            [p stroke];
            [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(c.x - 1, c.y - 4.5, 2, 2)] fill];
            return;
        }
        case TTRIconGear:  // three sliders: the options
            for (int i = 0; i < 3; ++i) {
                const CGFloat y = c.y - 5 + i * 5, knob = c.x + (i == 1 ? 3 : -3);
                [p moveToPoint:NSMakePoint(c.x - 7, y)];
                [p lineToPoint:NSMakePoint(c.x + 7, y)];
                [[NSBezierPath bezierPathWithOvalInRect:NSMakeRect(knob - 2, y - 2, 4, 4)] fill];
            }
            [p stroke];
            return;
        case TTRIconView:
            if (_isOn) {  // lines
                for (int i = 0; i < 4; ++i) { [p moveToPoint:NSMakePoint(c.x - 7, c.y - 6 + i * 4)]; [p lineToPoint:NSMakePoint(c.x + 7 - (i % 2) * 3, c.y - 6 + i * 4)]; }
                [p stroke];
            } else {  // spectrum: a grid of cells
                for (int row = 0; row < 3; ++row)
                    for (int col = 0; col < 3; ++col) [[NSBezierPath bezierPathWithRect:NSMakeRect(c.x - 7 + col * 5.3, c.y - 7 + row * 5.3, 3.6, 3.6)] fill];
            }
            return;
        default: return;
    }
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    NSColor *fill = _hover ? TTR_HOVER : TTR_PANEL_RAISED, *border = TTR_BORDER, *text = TTR_TEXT, *glyph = TTR_DIM;
    if (_kind == TTRButtonKindPlay) glyph = TTR_TEXT;
    if (_kind == TTRButtonKindDrag) { fill = _hover ? TTRHex(0x5CF0D6) : TTR_ACCENT; border = fill; text = TTR_ON_ACCENT; glyph = TTR_ON_ACCENT; }
    if (_isOn && _kind != TTRButtonKindDrag) { fill = TTR_ACCENT_TINT; border = TTR_ACCENT_TINT; text = TTR_ACCENT; glyph = TTR_ACCENT; }  // a filled background marks an active toggle
    if (_kind == TTRButtonKindIcon && !_isOn) text = TTR_DIM;
    if (_pressed) fill = [fill blendedColorWithFraction:0.2 ofColor:[NSColor blackColor]];
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSetAlpha(ctx, _isEnabled ? 1.0 : 0.38);
    TTRFillRounded(b, kTTRRadius, fill);
    TTRStrokeRounded(b, kTTRRadius, border, 1.0);
    if (_kind == TTRButtonKindIcon) { [self drawIconInRect:b color:_isOn || _hover ? (_isOn ? TTR_ACCENT : TTR_TEXT) : TTR_DIM]; return; }

    NSFont* font = TTRFont(12, NSFontWeightSemibold);
    for (CGFloat size = 11; size >= 9 && TTRTextWidth(_title, font) > b.size.width - 10; size -= 1) font = TTRFont(size, NSFontWeightSemibold);  // long titles shrink instead of being cut
    const CGFloat glyphW = (_kind == TTRButtonKindPlay || _kind == TTRButtonKindDrag) ? 12 : 0, gap = glyphW > 0 ? 8 : 0;
    const CGFloat textW = TTRTextWidth(_title, font), groupW = MIN(b.size.width - 8, glyphW + gap + textW);
    const CGFloat x0 = floor((b.size.width - groupW) / 2.0);
    if (glyphW > 0) [self drawGlyphInRect:NSMakeRect(x0, floor((b.size.height - 12) / 2.0), 12, 12) color:glyph];
    TTRDrawText(_title, NSMakeRect(x0 + glyphW + gap, 0, groupW - glyphW - gap + 2, b.size.height), font, text);
}

@end

// ============================================================ TTRDragButton_v1

@interface TTRDragButton_v1 () <NSDraggingSource>
@end

@implementation TTRDragButton_v1 {
    NSPoint _downPoint;
    BOOL _dragStarted, _armed;
}

- (void)mouseDown:(NSEvent*)event {
    _downPoint = [self convertPoint:event.locationInWindow fromView:nil];
    _dragStarted = NO;
    _armed = YES;
    [super mouseDown:event];
}

- (void)mouseDragged:(NSEvent*)event {
    if (!_armed || _dragStarted) return;
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (hypot(p.x - _downPoint.x, p.y - _downPoint.y) < kDragThreshold) return;
    _dragStarted = YES;
    _armed = NO;
    [self mouseExited:event];
    if (!self.isEnabled) { if (self.onDragRejected) self.onDragRejected(); return; }
    NSURL* url = self.dragFileProvider ? self.dragFileProvider() : nil;
    if (!url) { if (self.onDragRejected) self.onDragRejected(); return; }
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
