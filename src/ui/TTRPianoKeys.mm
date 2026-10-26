#import "ui/TTRPianoKeys.h"

#import "core/Types.h"
#import "ui/TTRTheme.h"
#import "ui/TTRViewportInput.h"

@implementation TTRPianoKeys_v1 {
    std::shared_ptr<TTRViewport> _vp;
    int _pressedPitch;
}

- (instancetype)initWithViewport:(std::shared_ptr<TTRViewport>)viewport {
    self = [super initWithFrame:NSMakeRect(0, 0, 54, 400)];
    if (self) {
        _vp = std::move(viewport);
        _pressedPitch = -1;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (int)pitchAtY:(CGFloat)y {
    const int pitch = int(std::lround(_vp->pitchForY(y)));
    return pitch >= ttr::kMinNote && pitch <= ttr::kMaxNote ? pitch : -1;
}

- (void)mouseDown:(NSEvent*)event {
    const int pitch = [self pitchAtY:[self convertPoint:event.locationInWindow fromView:nil].y];
    if (pitch < 0) return;
    _pressedPitch = pitch;
    self.needsDisplay = YES;
    if (_onKeyClick) _onKeyClick(pitch);
}
- (void)mouseUp:(NSEvent*)event { _pressedPitch = -1; self.needsDisplay = YES; }

- (void)scrollWheel:(NSEvent*)event {
    TTRHandleWheel(event, _vp.get(), NSMakePoint(0, [self convertPoint:event.locationInWindow fromView:nil].y), self.bounds.size, YES, nil);
    if (_onScroll) _onScroll();
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    [TTR_PANEL setFill];
    NSRectFill(b);
    const CGFloat rowH = _vp->rowH;
    const int hoverPitch = _vp->hasCursor ? int(std::lround(_vp->cursorPitch)) : -1;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, b);
    NSDictionary* cAttrs = TTRTextAttrs(TTRFont(9, NSFontWeightSemibold), TTRHex(0x4B5361));
    for (int p = ttr::kMaxNote; p >= ttr::kMinNote; --p) {
        const CGFloat y = _vp->yTopForPitch(p);
        if (y > b.size.height || y + rowH < 0) continue;
        const BOOL lit = p == _pressedPitch, hovered = p == hoverPitch;
        [(lit ? TTRHex(0x2EE6C5) : hovered ? TTRHex(0x8CF5E4) : TTRHex(0xD9DDE4)) setFill];
        NSRectFill(NSMakeRect(0, y, b.size.width, rowH));
        if (TTRIsBlackKey(p)) {
            [(lit ? TTRHex(0x1E9C86) : hovered ? TTRHex(0x2EE6C5) : TTRHex(0x23282F)) setFill];
            [[NSBezierPath bezierPathWithRoundedRect:NSMakeRect(-4, y + 0.5, b.size.width * 0.62 + 4, rowH - 1) xRadius:3 yRadius:3] fill];
        } else {
            [TTRHex(0xA3ACBA) setFill];
            NSRectFill(NSMakeRect(0, y + rowH - 1, b.size.width, 1));
        }
        if (hovered) { [TTR_ACCENT setFill]; NSRectFill(NSMakeRect(b.size.width - 6, y + 1, 6, rowH - 2)); }  // the key of the hovered row lights up
        if (p % 12 == 0) {
            NSString* name = TTRNoteName(p);
            const CGFloat w = TTRTextWidth(name, TTRFont(9, NSFontWeightSemibold));
            [name drawAtPoint:NSMakePoint(b.size.width - 10 - w, y + (rowH - 11) / 2 - 0.5) withAttributes:cAttrs];
        }
    }
    CGContextRestoreGState(ctx);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(b.size.width - 1, 0, 1, b.size.height));
}

@end
