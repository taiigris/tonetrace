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

// Narrow keyboard at the right edge: white keys as one light column with thin dividers, black keys as shorter dark bars from the inside;
// the C labels in small type to its left.
static const CGFloat kStripWidth = 16.0, kLabelGap = 4.0, kBlackKeyShare = 0.62;
static const uint32_t kPianoBackground = 0x08090B, kWhiteKey = 0xD2D6DC, kDivider = 0x7C828C, kBlackKey = 0x14161A, kLitKey = 0x2EE6C5, kPressedKey = 0x1E9C86, kLabelColor = 0x6A7280;

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    [TTRHex(kPianoBackground) setFill];
    NSRectFill(b);
    const CGFloat rowH = _vp->rowH, stripX = b.size.width - kStripWidth, blackW = round(kStripWidth * kBlackKeyShare);
    const int hoverPitch = _vp->hasCursor ? int(std::lround(_vp->cursorPitch)) : -1;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, b);
    NSDictionary* cAttrs = TTRTextAttrs(TTRFont(8, NSFontWeightMedium), TTRHex(kLabelColor));
    const CGFloat top = MAX(0.0, _vp->yTopForPitch(ttr::kMaxNote)), bottom = MIN(b.size.height, _vp->yTopForPitch(ttr::kMinNote) + rowH);
    [TTRHex(kWhiteKey) setFill];
    NSRectFill(NSMakeRect(stripX, top, kStripWidth, MAX(0.0, bottom - top)));
    for (int p = ttr::kMaxNote; p >= ttr::kMinNote; --p) {
        const CGFloat y = _vp->yTopForPitch(p);
        if (y > b.size.height || y + rowH < 0) continue;
        const BOOL lit = p == _pressedPitch, hovered = p == hoverPitch;
        const int pc = p % 12;
        if (TTRIsBlackKey(p)) {
            [TTRHex(kDivider) setFill];  // the divider between the two white keys behind this black key
            NSRectFill(NSMakeRect(stripX, floor(y + rowH / 2), kStripWidth, 1));
            [TTRHex(lit ? kPressedKey : hovered ? kLitKey : kBlackKey) setFill];
            NSRectFill(NSMakeRect(stripX + kStripWidth - blackW, y + 0.5, blackW, MAX(1.0, rowH - 1)));
        } else {
            if (lit || hovered) {
                [TTRHex(lit ? kPressedKey : kLitKey) setFill];
                NSRectFill(NSMakeRect(stripX, y + 0.5, kStripWidth, MAX(1.0, rowH - 1)));
            }
            if (pc == 0 || pc == 5) {  // C and F start a white key right after B / E
                [TTRHex(kDivider) setFill];
                NSRectFill(NSMakeRect(stripX, y + rowH - 1, kStripWidth, 1));
            }
        }
        if (pc == 0) {
            NSString* name = TTRNoteName(p);
            const CGFloat w = TTRTextWidth(name, TTRFont(8, NSFontWeightMedium));
            [name drawAtPoint:NSMakePoint(stripX - kLabelGap - w, y + (rowH - 10) / 2 - 0.5) withAttributes:cAttrs];
        }
    }
    CGContextRestoreGState(ctx);
}

@end
