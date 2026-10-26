#import "ui/TTRAbout.h"

#import <QuartzCore/QuartzCore.h>

#include <algorithm>

#import "core/Version.h"
#import "ui/TTRControls.h"
#import "ui/TTRLogo.h"
#import "ui/TTRTheme.h"

static const CFTimeInterval kFadeSeconds = 0.15;
static const unsigned short kKeyCodeEscape = 53;

static CGContextRef currentContext() { return NSGraphicsContext.currentContext.CGContext; }

// ============================================================ logo in the panel header

@implementation TTRLogoView_v1

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (BOOL)isOpaque { return NO; }
- (void)resetCursorRects { [self addCursorRect:self.bounds cursor:NSCursor.pointingHandCursor]; }
- (void)mouseDown:(NSEvent*)event {}
- (void)mouseUp:(NSEvent*)event {
    if (NSPointInRect([self convertPoint:event.locationInWindow fromView:nil], self.bounds) && _onClick) _onClick();
}
- (void)drawRect:(NSRect)dirty { TTRDrawWordmark(currentContext(), self.bounds); }

@end

// ============================================================ text link

@interface TTRLinkView_v1 : NSView
@property(nonatomic, copy) NSString* title;
@property(nonatomic, copy) void (^onClick)(void);
@end

@implementation TTRLinkView_v1 {
    BOOL _isHovered;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)resetCursorRects { [self addCursorRect:self.bounds cursor:NSCursor.pointingHandCursor]; }
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
- (void)mouseEntered:(NSEvent*)event { _isHovered = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent*)event { _isHovered = NO; self.needsDisplay = YES; }
- (void)mouseDown:(NSEvent*)event {}
- (void)mouseUp:(NSEvent*)event {
    if (NSPointInRect([self convertPoint:event.locationInWindow fromView:nil], self.bounds) && _onClick) _onClick();
}
- (void)drawRect:(NSRect)dirty {
    NSMutableDictionary* attrs = [TTRTextAttrs(TTRFont(12, NSFontWeightMedium), _isHovered ? TTR_TEXT : TTR_ACCENT) mutableCopy];
    attrs[NSUnderlineStyleAttributeName] = @(_isHovered ? NSUnderlineStyleSingle : NSUnderlineStyleNone);
    const CGFloat h = ceil(TTRFont(12, NSFontWeightMedium).ascender - TTRFont(12, NSFontWeightMedium).descender);
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = NSTextAlignmentCenter;
    attrs[NSParagraphStyleAttributeName] = style;
    [_title drawInRect:NSMakeRect(0, floor((self.bounds.size.height - h) / 2), self.bounds.size.width, h) withAttributes:attrs];
}

@end

// ============================================================ About overlay

static const CGFloat kCardW = 360, kCardH = 340, kMarkSide = 92, kLetteringW = 214, kCloseSide = 24, kLinkH = 22;

@implementation TTRAboutView_v1 {
    TTRButton_v1* _closeButton;
    TTRLinkView_v1 *_diagnosticsLink, *_logLink;
    NSResponder* _previousResponder;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.hidden = YES;
    self.wantsLayer = YES;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    __weak TTRAboutView_v1* weak = self;
    _closeButton = [[TTRButton_v1 alloc] initWithTitle:@"✕" kind:TTRButtonKindNormal];
    _closeButton.toolTip = @"Close";
    _closeButton.onClick = ^{ [weak dismiss]; };
    _diagnosticsLink = [[TTRLinkView_v1 alloc] initWithFrame:NSZeroRect];
    _diagnosticsLink.title = @"Diagnostics";
    _diagnosticsLink.toolTip = @"Open the diagnostics panel";
    _diagnosticsLink.onClick = ^{ [weak dismiss]; if (weak.onOpenDiagnostics) weak.onOpenDiagnostics(); };
    _logLink = [[TTRLinkView_v1 alloc] initWithFrame:NSZeroRect];
    _logLink.title = @"Open log folder";
    _logLink.toolTip = @"Show ~/Library/Logs/Tonetrace in the Finder";
    _logLink.onClick = ^{ if (weak.onOpenLogFolder) weak.onOpenLogFolder(); };
    for (NSView* v in @[ _closeButton, _diagnosticsLink, _logLink ]) [self addSubview:v];
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (NSRect)cardRect {
    const NSSize s = self.bounds.size;
    const CGFloat w = std::min<CGFloat>(kCardW, s.width - 20), h = std::min<CGFloat>(kCardH, s.height - 20);
    return NSMakeRect(floor((s.width - w) / 2), floor((s.height - h) / 2), w, h);
}

- (void)layoutContent {
    const NSRect c = [self cardRect];
    _closeButton.frame = NSMakeRect(NSMaxX(c) - 12 - kCloseSide, c.origin.y + 12, kCloseSide, kCloseSide);
    const CGFloat linksY = NSMaxY(c) - 24 - kLinkH, w1 = 90, w2 = 112, gap = 20, x = floor(NSMidX(c) - (w1 + gap + w2) / 2);
    _diagnosticsLink.frame = NSMakeRect(x, linksY, w1, kLinkH);
    _logLink.frame = NSMakeRect(x + w1 + gap, linksY, w2, kLinkH);
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize { [self layoutContent]; }

// ------------------------------------------------------------ show / hide

- (void)show {
    if (_isShowing) return;
    _isShowing = YES;
    [self layoutContent];
    self.hidden = NO;
    self.needsDisplay = YES;
    _previousResponder = self.window.firstResponder;
    [self.window makeFirstResponder:self];
    [self fadeFrom:0 to:1];
}

- (void)dismiss {
    if (!_isShowing) return;
    _isShowing = NO;
    NSResponder* restore = _previousResponder;
    _previousResponder = nil;
    NSResponder* current = self.window.firstResponder;
    if (current == self || ([current isKindOfClass:NSView.class] && [(NSView*)current isDescendantOf:self]))
        [self.window makeFirstResponder:restore ?: self.superview];
    [self fadeFrom:1 to:0];
}

- (void)fadeFrom:(float)from to:(float)to {
    __weak TTRAboutView_v1* weak = self;
    [CATransaction begin];
    [CATransaction setAnimationDuration:kFadeSeconds];
    [CATransaction setCompletionBlock:^{
        TTRAboutView_v1* strong = weak;
        if (strong && !strong->_isShowing) strong.hidden = YES;
    }];
    CABasicAnimation* fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
    fade.fromValue = @(from);
    fade.toValue = @(to);
    fade.duration = kFadeSeconds;
    fade.timingFunction = [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseInEaseOut];
    self.layer.opacity = to;
    [self.layer addAnimation:fade forKey:@"fade"];
    [CATransaction commit];
}

// ------------------------------------------------------------ events

- (void)mouseDown:(NSEvent*)event {
    if (!NSPointInRect([self convertPoint:event.locationInWindow fromView:nil], [self cardRect])) [self dismiss];
}
- (void)keyDown:(NSEvent*)event {
    if (event.keyCode == kKeyCodeEscape) [self dismiss];
    else [super keyDown:event];  // swallow everything else: the editor behind the dimmed backdrop is not usable
}
- (void)cancelOperation:(id)sender { [self dismiss]; }

// ------------------------------------------------------------ drawing

- (void)drawRect:(NSRect)dirty {
    [TTRHex(0x000000, 0.62) setFill];
    NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
    const NSRect c = [self cardRect];
    TTRFillRounded(c, 14, TTR_PANEL);
    TTRStrokeRounded(c, 14, TTR_BORDER, 1.0);

    CGContextRef ctx = currentContext();
    CGFloat y = c.origin.y + 34;
    TTRDrawMark(ctx, CGRectMake(floor(NSMidX(c) - kMarkSide / 2), y, kMarkSide, kMarkSide));
    y += kMarkSide + 20;
    CGColorRef text = TTR_TEXT.CGColor;
    TTRDrawLettering(ctx, CGRectMake(floor(NSMidX(c) - kLetteringW / 2), y, kLetteringW, kLetteringW / kTTRLetteringAspect), text);
    y += kLetteringW / kTTRLetteringAspect + 22;

    NSString* version = [NSString stringWithFormat:@"Version %s (Build %s)", TTR_VERSION_STRING, TTR_BUILD_HASH];
    TTRDrawText(version, NSMakeRect(c.origin.x, y, c.size.width, 18), TTRFont(13, NSFontWeightSemibold), TTR_TEXT, NSTextAlignmentCenter);
    y += 24;
    TTRDrawText(@"Trace the notes in any sample", NSMakeRect(c.origin.x, y, c.size.width, 18), TTRFont(12), TTR_DIM, NSTextAlignmentCenter);
    y += 24;
    TTRDrawText(@"© 2026 Tonetrace", NSMakeRect(c.origin.x, y, c.size.width, 18), TTRFont(11), TTRHex(0x5E6675), NSTextAlignmentCenter);
}

@end
