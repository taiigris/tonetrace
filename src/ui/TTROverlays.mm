#import "ui/TTROverlays.h"

#import "core/Logger.h"
#import "core/Version.h"
#import "ui/TTRLogo.h"
#import "ui/TTRControls.h"
#import "ui/TTRTheme.h"

static const CFTimeInterval kBannerSeconds = 9.0;
static const CGFloat kCloseWidth = 26;

@implementation TTRBannerView_v1 {
    ttr::NoticeLevel _level;
    NSTimer* _fade;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.hidden = YES;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isShowing { return !self.hidden; }

- (void)dealloc { [_fade invalidate]; }

- (void)showText:(NSString*)text level:(ttr::NoticeLevel)level {
    _text = [text copy];
    _level = level;
    self.hidden = NO;
    self.needsDisplay = YES;
    [_fade invalidate];
    _fade = nil;
    if (level != ttr::NoticeLevel::Error) {
        __weak TTRBannerView_v1* weak = self;
        _fade = [NSTimer scheduledTimerWithTimeInterval:kBannerSeconds repeats:NO block:^(NSTimer*) { [weak hideBanner]; }];
    }
}

- (void)hideBanner {
    [_fade invalidate];
    _fade = nil;
    self.hidden = YES;
}

- (void)mouseDown:(NSEvent*)event {
    if ([self convertPoint:event.locationInWindow fromView:nil].x > self.bounds.size.width - kCloseWidth - 6) [self hideBanner];
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    const uint32_t colors[] = {0x1F6F78, 0x8A5A12, 0x8E2433};
    TTRFillRounded(b, 6, TTRHex(colors[int(_level)], 0.96));
    static NSString* const marks[] = {@"i", @"!", @"!"};
    TTRDrawText(marks[int(_level)], NSMakeRect(8, 0, 16, b.size.height), TTRFont(13, NSFontWeightHeavy), TTR_TEXT, NSTextAlignmentCenter);
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary* attrs = [TTRTextAttrs(TTRFont(12, NSFontWeightMedium), TTR_TEXT) mutableCopy];
    attrs[NSParagraphStyleAttributeName] = style;
    [_text ?: @"" drawInRect:NSMakeRect(30, 6, b.size.width - 30 - kCloseWidth - 8, b.size.height - 8) withAttributes:attrs];
    TTRDrawText(@"×", NSMakeRect(b.size.width - kCloseWidth - 6, 0, kCloseWidth, b.size.height), TTRFont(16, NSFontWeightMedium), TTR_TEXT, NSTextAlignmentCenter);
}

@end

// ============================================================ diagnostics panel

static const CGFloat kDiagMaxWidth = 560, kDiagMaxHeight = 360, kDiagPad = 14, kDiagButtonH = 24, kDiagLineH = 17;

@implementation TTRDiagnosticsView_v1 {
    ttr::DiagnosticsSnapshot _snap;
    NSArray<TTRButton_v1*>* _buttons;
}

- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (!self) return nil;
    self.hidden = YES;
    self.wantsLayer = YES;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    __weak TTRDiagnosticsView_v1* weak = self;
    NSMutableArray* buttons = [NSMutableArray array];
    void (^add)(NSString*, NSString*, void (^)(void)) = ^(NSString* title, NSString* tip, void (^action)(void)) {
        TTRButton_v1* b = [[TTRButton_v1 alloc] initWithTitle:title kind:TTRButtonKindNormal];
        b.toolTip = tip;
        b.onClick = action;
        [self addSubview:b];
        [buttons addObject:b];
    };
    add(@"Copy report", @"Copy the diagnostics text to the clipboard", ^{ [weak copyReport]; });
    add(@"Save report…", @"Save the diagnostics text as a file", ^{ [weak saveReport]; });
    add(@"Open log folder", @"Show ~/Library/Logs/Tonetrace in the Finder", ^{ [weak openLogFolder]; });
    add(@"Close", @"Close this panel", ^{ if (weak.onClose) weak.onClose(); });
    _buttons = buttons;
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (void)mouseDown:(NSEvent*)event {}  // the panel swallows clicks so nothing happens behind it

- (NSRect)panelRect {
    const NSSize s = self.bounds.size;
    const CGFloat w = MIN(kDiagMaxWidth, s.width - 20), h = MIN(kDiagMaxHeight, s.height - 20);
    return NSMakeRect(floor((s.width - w) / 2), floor((s.height - h) / 2), w, h);
}

- (void)layoutButtons {
    const NSRect p = [self panelRect];
    const CGFloat gap = 6, widths[] = {96, 98, 112, 64};
    CGFloat x = p.origin.x + kDiagPad;
    const CGFloat y = NSMaxY(p) - kDiagPad - kDiagButtonH;
    for (NSUInteger i = 0; i < _buttons.count; ++i) {
        _buttons[i].frame = NSMakeRect(x, y, widths[i], kDiagButtonH);
        x += widths[i] + gap;
    }
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize { [self layoutButtons]; }
- (void)setHidden:(BOOL)hidden { [super setHidden:hidden]; if (!hidden) { [self layoutButtons]; self.needsDisplay = YES; } }

- (void)updateWithSnapshot:(const ttr::DiagnosticsSnapshot&)snapshot {
    _snap = snapshot;
    if (!self.hidden) self.needsDisplay = YES;
}

- (NSString*)reportText { return [NSString stringWithUTF8String:ttr::formatReport(_snap).c_str()]; }

- (void)copyReport {
    NSPasteboard* pb = NSPasteboard.generalPasteboard;
    [pb clearContents];
    const BOOL ok = [pb setString:[self reportText] forType:NSPasteboardTypeString];
    if (_onMessage) _onMessage(ok ? @"Diagnostics report copied to the clipboard" : @"Could not copy the report");
}

- (void)saveReport {
    NSSavePanel* panel = [NSSavePanel savePanel];
    panel.nameFieldStringValue = @"Tonetrace report.txt";
    NSString* text = [self reportText];
    __weak TTRDiagnosticsView_v1* weak = self;
    void (^finish)(NSModalResponse) = ^(NSModalResponse r) {
        if (r != NSModalResponseOK || !panel.URL) return;
        NSError* error = nil;
        const BOOL ok = [text writeToURL:panel.URL atomically:YES encoding:NSUTF8StringEncoding error:&error];
        if (weak.onMessage) weak.onMessage(ok ? [NSString stringWithFormat:@"Report saved to %@", panel.URL.lastPathComponent]
                                              : [NSString stringWithFormat:@"Could not save the report: %@", error.localizedDescription]);
    };
    if (self.window) [panel beginSheetModalForWindow:self.window completionHandler:finish];
    else finish([panel runModal]);
}

- (void)openLogFolder {
    NSString* dir = [NSString stringWithUTF8String:ttr::Logger::instance().directory().c_str()];
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    const BOOL ok = [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:dir isDirectory:YES]];
    if (!ok && _onMessage) _onMessage(@"Could not open the log folder");
}

static NSString* formatBytesMb(double mb) { return [NSString stringWithFormat:@"%.1f MB", mb]; }

- (void)drawRect:(NSRect)dirty {
    [TTRHex(0x000000, 0.55) setFill];
    NSRectFillUsingOperation(self.bounds, NSCompositingOperationSourceOver);
    const NSRect p = [self panelRect];
    TTRFillRounded(p, 10, TTR_PANEL);
    TTRStrokeRounded(p, 10, TTR_BORDER, 1.0);
    static const CGFloat kHeaderMark = 20;
    TTRDrawMark(NSGraphicsContext.currentContext.CGContext, CGRectMake(p.origin.x + kDiagPad, p.origin.y + 10, kHeaderMark, kHeaderMark));
    TTRDrawText(@"DIAGNOSTICS", NSMakeRect(p.origin.x + kDiagPad + kHeaderMark + 8, p.origin.y + 10, 200, kHeaderMark), TTRFont(11, NSFontWeightHeavy), TTR_ACCENT, NSTextAlignmentLeft, 1.8);

    const ttr::DiagnosticsSnapshot& d = _snap;
    NSString* host = d.host.empty() ? @"unknown" : [NSString stringWithUTF8String:d.host.c_str()];
    NSString* midi = d.lastMidiPath.empty() ? @"-" : [NSString stringWithUTF8String:d.lastMidiPath.c_str()];
    NSString* lastError = d.lastError.empty() ? @"-" : [NSString stringWithUTF8String:d.lastError.c_str()];
    NSArray<NSArray<NSString*>*>* rows = @[
        @[ @"Version", [NSString stringWithFormat:@"%s (build %s)  ·  instance #%d", TTR_VERSION_STRING, TTR_BUILD_HASH, d.instanceId] ],
        @[ @"Host", host ],
        @[ @"Audio", [NSString stringWithFormat:@"%.0f Hz  ·  block %d (max %d)  ·  %d ch  ·  %@", d.sampleRate, d.lastBlock, d.maxBlock, d.channels, d.isOffline ? @"offline" : @"realtime"]],
        @[ @"DSP load", [NSString stringWithFormat:@"avg %.2f %%  ·  peak %.2f %%  ·  overruns %llu of %llu blocks", d.dspAvgPercent, d.dspPeakPercent, (unsigned long long)d.overruns, (unsigned long long)d.blocks] ],
        @[ @"Worker lag", [NSString stringWithFormat:@"%.0f ms behind the live audio", d.workerDelayMs] ],
        @[ @"Recording", [NSString stringWithFormat:@"%.1f s  ·  capture memory %@", d.recordedSeconds, formatBytesMb(d.captureMegabytes)] ],
        @[ @"Editor", [NSString stringWithFormat:@"%.0f fps  ·  frame %.2f ms", d.uiFps, d.uiFrameMs] ],
        @[ @"Last MIDI file", midi ],
        @[ @"Last error", lastError ],
    ];
    CGFloat y = p.origin.y + 40;
    const CGFloat labelW = 96, valueX = p.origin.x + kDiagPad + labelW;
    for (NSArray<NSString*>* row in rows) {
        TTRDrawText(row[0], NSMakeRect(p.origin.x + kDiagPad, y, labelW, kDiagLineH), TTRFont(11, NSFontWeightMedium), TTR_DIM);
        NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
        style.lineBreakMode = NSLineBreakByTruncatingMiddle;
        NSMutableDictionary* attrs = [TTRTextAttrs(TTRMono(11, NSFontWeightRegular), TTR_TEXT) mutableCopy];
        attrs[NSParagraphStyleAttributeName] = style;
        [row[1] drawInRect:NSMakeRect(valueX, y + 2, NSMaxX(p) - valueX - kDiagPad, kDiagLineH) withAttributes:attrs];
        y += kDiagLineH + 4;
    }
}

@end
