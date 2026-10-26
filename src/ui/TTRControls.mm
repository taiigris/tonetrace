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

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    NSColor *fill = _hover ? TTRHex(0x2F3744) : TTR_PANEL_RAISED, *border = TTR_BORDER, *text = TTR_TEXT, *glyph = TTR_DIM;
    if (_kind == TTRButtonKindPlay) glyph = TTR_ACCENT;
    if (_kind == TTRButtonKindDrag) { fill = TTRHex(0x2EE6C5, _hover ? 0.22 : 0.14); border = TTR_ACCENT; text = TTR_ACCENT; glyph = TTR_ACCENT; }
    if (_isOn && _kind == TTRButtonKindPlay) { fill = TTR_ACCENT; border = TTR_ACCENT; text = TTRHex(0x0B1512); glyph = TTRHex(0x0B1512); }
    if (_isOn && _kind == TTRButtonKindNormal) { fill = TTRHex(0x2EE6C5, 0.20); border = TTR_ACCENT; text = TTR_ACCENT; }  // toggle on
    if (_pressed) fill = [fill blendedColorWithFraction:0.25 ofColor:[NSColor blackColor]];
    const CGFloat alpha = _isEnabled ? 1.0 : 0.38;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSetAlpha(ctx, alpha);
    TTRFillRounded(b, 7, fill);
    TTRStrokeRounded(b, 7, border, 1.0);

    NSFont* font = TTRFont(12, NSFontWeightSemibold);
    const CGFloat glyphW = _kind == TTRButtonKindNormal ? 0 : 12, gap = glyphW > 0 ? 7 : 0;
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
