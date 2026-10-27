// Drawing code of the note map (CoreGraphics / NSBezierPath).
#import <QuartzCore/QuartzCore.h>

#import "core/Logger.h"
#import "ui/TTRHeatmap.h"
#import "ui/TTRNoteMapInternal.h"
#import "ui/TTRTheme.h"

namespace {

const CGFloat kMinBeatPixels = 7.0, kMinBarPixels = 10.0;
const double kFrameAverageWeight = 0.1;

void fillRect(CGContextRef ctx, CGRect r, uint32_t rgb, CGFloat alpha) {
    CGContextSetRGBFillColor(ctx, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, alpha);
    CGContextFillRect(ctx, r);
}

NSString* formatClock(double t) {
    const int total = int(t + 1e-6);
    return [NSString stringWithFormat:@"%d:%02d", total / 60, total % 60];
}

}  // namespace

@implementation TTRNoteMapView_v1 (Drawing)

- (void)drawContent {
    const CFTimeInterval started = CACurrentMediaTime();
    @try {
        [self drawContentInContext];
    } @catch (NSException* e) {  // a drawing bug must never take the host down with it
        ttr::logError(_kernel->instanceId(), "note map drawing failed: %s: %s", e.name.UTF8String, e.reason.UTF8String);
        _kernel->diagnostics().setLastError(std::string("drawing failed: ") + e.name.UTF8String);
    }
    const double ms = (CACurrentMediaTime() - started) * 1000.0;
    _vp->frameMsAvg = _vp->drawCount == 0 ? ms : _vp->frameMsAvg + (ms - _vp->frameMsAvg) * kFrameAverageWeight;
    ++_vp->drawCount;
}

- (void)drawContentInContext {
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    const NSRect b = self.bounds;
    [TTR_BG setFill];
    NSRectFill(b);
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, b);
    [self drawHeat:ctx];
    [self drawBeatGrid:ctx];
    [self drawHover:ctx];
    [self drawNotes];
    [self drawGhostAndLabel:ctx];
    [self drawRecordingEdge:ctx];
    [self drawPlayhead:ctx];
    if (_gesture == TTRGestureLasso && _gestureStarted) {
        const NSRect l = [self lassoRect];
        fillRect(ctx, l, 0x2EE6C5, 0.12);
        TTRStrokeRounded(l, 1, TTRHex(0x2EE6C5, 0.9), 1.0);
    }
    [self drawCrosshair:ctx];
    [self drawOverlay];
    CGContextRestoreGState(ctx);
}

// ------------------------------------------------------------ heatmap

- (void)drawHeat:(CGContextRef)ctx {
    const NSRect b = self.bounds;
    const BOOL isLive = _status != ttr::KernelStatus::Idle;
    if (_mv.linePoints && _mv.lineFrames > 0) { [self drawLines:ctx live:isLive]; return; }
    const BOOL useHarmonic = _kernel->param(ttr::kParamHideDrums) >= 0.5f && _mv.harmonic != nullptr;
    TTRHeatParams p;
    p.data = useHarmonic ? _mv.harmonic : _mv.raw;
    p.frames = useHarmonic ? _mv.harmonicFrames : _mv.frames;  // live: the harmonic part lags a few frames behind
    p.hopSec = _mv.hopSec;
    p.reference = _mv.peak;
    p.scrollSec = _vp->scrollSec; p.pxPerSec = _vp->pxPerSec; p.scrollY = _vp->scrollY; p.rowH = _vp->rowH;
    p.width = b.size.width; p.height = b.size.height;
    p.scale = self.window ? self.window.backingScaleFactor : 1.0;
    p.gain = _kernel->param(ttr::kParamGain);
    p.range = _kernel->param(ttr::kParamRange);
    std::shared_ptr<const void> owner;
    if (isLive) owner = _mv.hold[useHarmonic ? 1 : 0];
    else if (_mv.snapshot) owner = _mv.snapshot->spectrogram;
    _tiles.draw(ctx, p, std::move(owner), _mv.generation, isLive);
}

// Thin pitch lines on the piano-roll grid (heat map only for recordings without lines, e.g. restored long ones).
- (void)drawLines:(CGContextRef)ctx live:(BOOL)isLive {
    const NSRect b = self.bounds;
    TTRHeatParams p;
    p.linePoints = _mv.linePoints;
    p.lineCounts = _mv.lineCounts;
    p.frames = _mv.lineFrames;
    p.isLive = isLive;
    p.fundamentals = _kernel->lineView().fundamentals;
    p.hideDrums = _kernel->param(ttr::kParamHideDrums) >= 0.5f;
    p.tuningCents = float([self tuningCents]);
    p.hopSec = _mv.lines ? _mv.lines->hopSec : _mv.hopSec;
    p.reference = _mv.linePeak;
    p.scrollSec = _vp->scrollSec; p.pxPerSec = _vp->pxPerSec; p.scrollY = _vp->scrollY; p.rowH = _vp->rowH;
    p.width = b.size.width; p.height = b.size.height;
    p.scale = self.window ? self.window.backingScaleFactor : 1.0;
    p.gain = _kernel->param(ttr::kParamGain);
    p.range = _kernel->param(ttr::kParamRange);
    TTRDrawRowGrid(ctx, p);
    _tiles.draw(ctx, p, _mv.lineHold, _mv.generation, isLive);
}

- (void)drawBeatGrid:(CGContextRef)ctx {
    const double bps = _grid.beatsPerSec(), ppq0 = _grid.startPpq, beatPx = _vp->pxPerSec / bps;
    if (beatPx < kMinBeatPixels) return;
    const double t0 = _vp->timeForX(0), t1 = _vp->timeForX(self.bounds.size.width);
    for (long k = long(floor(ppq0 + t0 * bps)); k <= long(ceil(ppq0 + t1 * bps)); ++k) {
        const BOOL isBar = k % 4 == 0;
        if (isBar && beatPx * 4 < kMinBarPixels) continue;
        const CGFloat x = floor(_vp->xForTime((k - ppq0) / bps)) + 0.5;
        fillRect(ctx, CGRectMake(x, 0, 1, self.bounds.size.height), 0xFFFFFF, isBar ? 0.16 : 0.06);
    }
}

// ------------------------------------------------------------ notes

- (void)drawNotes {
    const std::vector<ttr::NoteEvent>& notes = [self notes];
    const NSRect b = self.bounds;
    const int hovered = [self hasHoverPoint] ? [self hitNoteIndexAt:[self cursorPoint]] : -1;
    NSFont* font = TTRFont(9, NSFontWeightSemibold);
    NSDictionary* labelAttrs = TTRTextAttrs(font, TTRHex(0xFFFFFF, 0.9));
    for (size_t i = 0; i < notes.size(); ++i) {
        const ttr::NoteEvent& n = notes[i];
        const NSRect row = [self rectForNote:n];
        if (NSMaxX(row) < 0 || row.origin.x > b.size.width || NSMaxY(row) < 0 || row.origin.y > b.size.height) continue;
        const NSRect bar = TTRNoteBarRect(row);
        TTRFillRounded(bar, MIN(1.5, bar.size.height / 3.0), n.selected ? TTRHex(0xFF9F43, 0.92) : TTRHex(0x2FBF5B, 0.88));
        if (!n.selected && int(i) != hovered) continue;  // the name only shows on hover or selection, beside the bar
        NSString* name = TTRNoteName(n.pitch);
        const CGFloat w = TTRTextWidth(name, font) + 8, h = 13;
        CGFloat x = NSMaxX(bar) + 4;
        if (x + w > b.size.width - 2) x = bar.origin.x - 4 - w;
        const NSRect pill = NSMakeRect(round(x), round(NSMidY(bar) - h / 2), w, h);
        TTRFillRounded(pill, 4, TTRHex(0x08090B, 0.78));
        [name drawAtPoint:NSMakePoint(pill.origin.x + 4, pill.origin.y + (h - 11) / 2 - 0.5) withAttributes:labelAttrs];
    }
}

// ------------------------------------------------------------ markers

- (void)drawRecordingEdge:(CGContextRef)ctx {
    if (_status != ttr::KernelStatus::Recording) return;
    const CGFloat x = floor(_vp->xForTime(_recorded)) + 0.5;
    fillRect(ctx, CGRectMake(x - 2, 0, 5, self.bounds.size.height), 0xFF4D5E, 0.14);
    fillRect(ctx, CGRectMake(x, 0, 1, self.bounds.size.height), 0xFF4D5E, 0.95);
}

- (void)drawPlayhead:(CGContextRef)ctx {
    if (!_kernel->previewing() || _status != ttr::KernelStatus::Idle) return;
    const CGFloat x = floor(_vp->xForTime(_playhead)) + 0.5;
    fillRect(ctx, CGRectMake(x - 3, 0, 7, self.bounds.size.height), 0xFFFFFF, 0.07);
    fillRect(ctx, CGRectMake(x, 0, 1.5, self.bounds.size.height), 0xFFFFFF, 0.9);
}

// The hovered row is highlighted by -drawHover; the pointer only gets a faint vertical time marker.
- (void)drawCrosshair:(CGContextRef)ctx {
    if (!_vp->hasCursor) return;
    fillRect(ctx, CGRectMake(floor(_vp->xForTime(_vp->cursorSec)) + 0.5, 0, 1, self.bounds.size.height), 0xFFFFFF, 0.22);
}

// ------------------------------------------------------------ overlays

- (void)drawCard:(NSRect)card {
    TTRFillRounded(card, 12, TTRHex(0x1B1F26, 0.95));
    TTRStrokeRounded(card, 12, TTR_BORDER);
}

- (void)drawOverlay {
    const NSRect grid = self.bounds;
    const NSPoint c = NSMakePoint(NSMidX(grid), NSMidY(grid));
    if (_status == ttr::KernelStatus::Analysing) {
        const NSRect card = NSMakeRect(c.x - 100, c.y - 38, 200, 76);
        [self drawCard:card];
        const double phase = fmod(CACurrentMediaTime() * 3.0, 2 * M_PI);
        NSBezierPath* arc = [NSBezierPath bezierPath];
        for (int i = 0; i <= 24; ++i) {
            const double a = phase + i * (1.5 * M_PI / 24);
            const NSPoint pt = NSMakePoint(card.origin.x + 38 + 11 * cos(a), c.y + 11 * sin(a));
            i == 0 ? [arc moveToPoint:pt] : [arc lineToPoint:pt];
        }
        arc.lineWidth = 3;
        arc.lineCapStyle = NSLineCapStyleRound;
        [TTR_ACCENT setStroke];
        [arc stroke];
        TTRDrawText(@"Analysing…", NSMakeRect(card.origin.x + 62, card.origin.y, 130, card.size.height), TTRFont(15, NSFontWeightSemibold), TTR_TEXT);
        return;
    }
    if (_status == ttr::KernelStatus::Recording) {
        NSString* badge = [NSString stringWithFormat:@"REC  %@", formatClock(_recorded)];
        const CGFloat bw = TTRTextWidth(badge, TTRMono(11, NSFontWeightBold), 1.0) + 34;
        const NSRect pill = NSMakeRect(14, 12, bw, 24);
        TTRFillRounded(pill, 12, TTRHex(0x14161B, 0.85));
        TTRStrokeRounded(pill, 12, TTRHex(0xFF4D5E, 0.8));
        TTRFillRounded(NSMakeRect(pill.origin.x + 10, pill.origin.y + 8, 8, 8), 4, TTR_RECORD);
        TTRDrawText(badge, NSMakeRect(pill.origin.x + 24, pill.origin.y, bw - 24, 24), TTRMono(11, NSFontWeightBold), TTR_TEXT, NSTextAlignmentLeft, 1.0);
        if (_mv.frames < 2)
            TTRDrawText(@"Listening… play your sample now", NSMakeRect(0, c.y - 12, grid.size.width, 24), TTRFont(15, NSFontWeightMedium), TTR_DIM, NSTextAlignmentCenter);
        return;
    }
    const BOOL hasNotes = ![self notes].empty();
    if (!_mv.snapshot || (!_mv.snapshot->hasRecording() && !hasNotes)) { [self drawEmptyState:c]; return; }
    if (!hasNotes) {
        NSString* hint = @"Click and drag on the bright lines to draw notes";
        const CGFloat w = TTRTextWidth(hint, TTRFont(12, NSFontWeightMedium)) + 32;
        const NSRect pill = NSMakeRect(round(c.x - w / 2), grid.size.height - 44, w, 28);
        TTRFillRounded(pill, 14, TTRHex(0x14161B, 0.88));
        TTRStrokeRounded(pill, 14, TTRHex(0x19E6A0, 0.6));
        TTRDrawText(hint, pill, TTRFont(12, NSFontWeightMedium), TTR_TEXT, NSTextAlignmentCenter);
    }
}

- (void)drawEmptyState:(NSPoint)c {
    static NSString* const steps[4] = {@"Record", @"Play your sample", @"Draw notes on the bright lines", @"Drag MIDI"};
    static NSString* const details[4] = {@"Press the red button and play the audio through the plugin.", @"The map fills while it plays. Press Record again to stop.",
                                         @"Click-drag on a bright line; notes snap to grid and line.", @"Drag the Drag MIDI button into a MIDI track."};
    const CGFloat cardW = MIN(460.0, self.bounds.size.width - 24), cardH = 236;
    const NSRect card = NSMakeRect(round(c.x - cardW / 2), round(c.y - cardH / 2), cardW, cardH);
    [self drawCard:card];
    TTRDrawText(@"Draw the notes of any sample", NSMakeRect(card.origin.x + 24, card.origin.y + 16, cardW - 48, 24), TTRFont(17, NSFontWeightSemibold), TTR_TEXT);
    for (int i = 0; i < 4; ++i) {
        const CGFloat y = card.origin.y + 52 + i * 44;
        const NSRect dot = NSMakeRect(card.origin.x + 24, y + 4, 26, 26);
        TTRFillRounded(dot, 13, TTRHex(0x2EE6C5, 0.14));
        TTRStrokeRounded(dot, 13, TTRHex(0x2EE6C5, 0.55));
        TTRDrawText([NSString stringWithFormat:@"%d", i + 1], dot, TTRMono(13, NSFontWeightBold), TTR_ACCENT, NSTextAlignmentCenter);
        TTRDrawText(steps[i], NSMakeRect(card.origin.x + 62, y, cardW - 86, 17), TTRFont(13, NSFontWeightSemibold), TTR_TEXT);
        TTRDrawText(details[i], NSMakeRect(card.origin.x + 62, y + 17, cardW - 86, 15), TTRFont(11), TTR_DIM);
    }
}

@end
