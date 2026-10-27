// Note map: the hovered row, the outline of the line under the pointer, the ghost note and the label at the pointer.
#include <algorithm>
#include <cmath>

#import "core/NoteEdit.h"
#import "ui/TTRNoteMapInternal.h"
#import "ui/TTRTheme.h"

static const CGFloat kLabelPadX = 8.0, kLabelHeight = 20.0, kLabelOffsetX = 14.0, kLabelOffsetY = 16.0, kGhostLabelHeight = 16.0;
static const double kFlashSeconds = 0.8, kDefaultGhostBeats = 0.25;

static NSString* formatCents(double cents) {
    const long rounded = lround(cents);
    if (rounded == 0) return @"0 ct";
    return [NSString stringWithFormat:@"%@%ld ct", rounded < 0 ? @"−" : @"+", labs(rounded)];
}

// "1/8" for half a beat: the musical length nearest to the note on a log scale.
static NSString* lengthName(double beats) {
    static NSString* const names[] = {@"1/32", @"1/16", @"1/8", @"1/4", @"1/2", @"1", @"2", @"4"};
    const int index = std::clamp(int(std::lround(std::log2(std::max(beats, 0.01) / 0.125))), 0, 7);
    return names[index];
}

@implementation TTRNoteMapView_v1 (Hover)

- (BOOL)hasHoverPoint { return _vp->hasCursor && _gesture == TTRGestureNone && _status == ttr::KernelStatus::Idle; }

- (NSPoint)cursorPoint { return NSMakePoint(_vp->xForTime(_vp->cursorSec), (ttr::kMaxNote + 0.5 - _vp->cursorPitch) * _vp->rowH - _vp->scrollY); }

- (ttr::LineHit)hoveredLine { return [self hasHoverPoint] ? [self lineAtPoint:[self cursorPoint]] : ttr::LineHit{}; }

- (BOOL)ghostNote:(ttr::NoteEvent*)out {
    if (![self hasHoverPoint]) return NO;
    const NSPoint cursor = [self cursorPoint];
    if ([self hitNoteIndexAt:cursor] >= 0) return NO;
    const ttr::LineHit hit = [self lineAtPoint:cursor];
    if (hit.track >= 0) { *out = [self lines]->noteForTrack(hit.track, [self tuningCents]); return YES; }
    const ttr::TimeGrid grid = _kernel->timeGrid();
    const double start = std::max(0.0, ttr::snapTime(_vp->cursorSec, grid));
    const double length = std::max(ttr::gridStepSec(grid), kDefaultGhostBeats / grid.beatsPerSec());
    *out = ttr::makeNote(std::clamp(int(std::lround(_vp->cursorPitch)), ttr::kMinNote, ttr::kMaxNote), start, start + length, grid, false);
    return YES;
}

- (void)strokeTrack:(int)track context:(CGContextRef)ctx width:(CGFloat)width color:(NSColor*)color {
    const ttr::PitchLines* lines = [self lines];
    if (!lines) return;
    std::vector<int> frames;
    const std::vector<ttr::LinePoint> points = lines->trackPoints(track, &frames);
    if (points.size() < 2) return;
    CGContextSaveGState(ctx);
    CGContextSetLineWidth(ctx, width);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGContextSetLineJoin(ctx, kCGLineJoinRound);
    CGContextSetStrokeColorWithColor(ctx, color.CGColor);
    for (size_t i = 0; i < points.size(); ++i) {
        const CGPoint pt = CGPointMake(_vp->xForTime(double(frames[i]) * lines->hopSec), [self yForMidi:points[i].midi]);
        i == 0 ? CGContextMoveToPoint(ctx, pt.x, pt.y) : CGContextAddLineToPoint(ctx, pt.x, pt.y);
    }
    CGContextStrokePath(ctx);
    CGContextRestoreGState(ctx);
}

- (void)drawHover:(CGContextRef)ctx {
    const NSRect b = self.bounds;
    if ([self hasHoverPoint]) {
        const int row = std::clamp(int(std::lround(_vp->cursorPitch)), ttr::kMinNote, ttr::kMaxNote);
        const CGFloat y = _vp->yTopForPitch(row);
        CGContextSetRGBFillColor(ctx, 1, 1, 1, 0.035);
        CGContextFillRect(ctx, CGRectMake(0, y, b.size.width, _vp->rowH));
        const ttr::LineHit hit = [self hoveredLine];
        if (hit.track >= 0) {
            [self strokeTrack:hit.track context:ctx width:4.0 color:TTRHex(0x2EE6C5, 0.16)];
            [self strokeTrack:hit.track context:ctx width:1.2 color:TTRHex(0xFFFFFF, 0.70)];
        }
    }
    const double age = CACurrentMediaTime() - _flashStart;
    if (_flashTrack >= 0 && age < kFlashSeconds) {
        const CGFloat fade = 1.0 - age / kFlashSeconds;
        [self strokeTrack:_flashTrack context:ctx width:7.0 color:TTRHex(0xFFFFFF, 0.22 * fade)];
        [self strokeTrack:_flashTrack context:ctx width:1.6 color:TTRHex(0xFFFFFF, 0.85 * fade)];
    }
}

- (void)drawGhostAndLabel:(CGContextRef)ctx {
    const NSRect b = self.bounds;
    ttr::NoteEvent ghost;
    if ([self ghostNote:&ghost]) {
        const NSRect r = TTRNoteBarRect([self rectForNote:ghost]);
        TTRFillRounded(r, MIN(1.5, r.size.height / 3.0), TTRHex(0x2FBF5B, 0.38));
        NSString* label = [self ghostLabel];
        NSFont* font = TTRFont(9, NSFontWeightBold);
        const CGFloat w = TTRTextWidth(label, font) + 2 * kLabelPadX;
        CGFloat x = NSMinX(r), y = NSMinY(r) - kGhostLabelHeight - 3;
        if (y < 2) y = NSMaxY(r) + 3;
        x = std::clamp(x, 2.0, std::max(2.0, b.size.width - w - 2));
        const NSRect pill = NSMakeRect(round(x), round(y), w, kGhostLabelHeight);
        TTRFillRounded(pill, kGhostLabelHeight / 2, TTRHex(0x0B0C0E, 0.88));
        TTRStrokeRounded(pill, kGhostLabelHeight / 2, TTRHex(0xFFFFFF, 0.22));
        TTRDrawText(label, pill, font, TTR_TEXT, NSTextAlignmentCenter);
    }
    NSString* text = [self hasHoverPoint] || _vp->hasCursor ? [self hoverLabel] : nil;
    if (!text || _gesture != TTRGestureNone) return;
    NSFont* font = TTRMono(11, NSFontWeightSemibold);
    const CGFloat w = TTRTextWidth(text, font) + 2 * kLabelPadX;
    const NSPoint c = [self cursorPoint];
    // below the pointer: the ghost note's label sits above its note, so the two never meet
    CGFloat x = c.x + kLabelOffsetX, y = c.y + kLabelOffsetY;
    if (x + w > b.size.width - 4) x = c.x - kLabelOffsetX - w;
    if (y + kLabelHeight > b.size.height - 4) y = c.y - kLabelOffsetY - kLabelHeight;
    y = std::clamp(y, 4.0, std::max(4.0, b.size.height - kLabelHeight - 4));
    const NSRect pill = NSMakeRect(round(x), round(y), w, kLabelHeight);
    TTRFillRounded(pill, 6, TTRHex(0x0B0C0E, 0.9));
    TTRStrokeRounded(pill, 6, TTRHex(0xFFFFFF, 0.28));
    TTRDrawText(text, pill, font, TTR_TEXT, NSTextAlignmentCenter);
}

@end

@implementation TTRNoteMapView_v1 (Inspection)

- (NSString*)hoverLabel {
    if (!_vp->hasCursor) return nil;
    const ttr::LineHit hit = [self hoveredLine];
    const int row = std::clamp(int(std::lround(_vp->cursorPitch)), ttr::kMinNote, ttr::kMaxNote);
    if (hit.track < 0) return TTRNoteName(row);
    const double tuning = [self tuningCents];
    const int lineRow = std::clamp(ttr::rowOfMidi(hit.midi, tuning), ttr::kMinNote, ttr::kMaxNote);
    return [NSString stringWithFormat:@"%@ %@", TTRNoteName(lineRow), formatCents(ttr::centsFromRow(hit.midi, tuning))];
}

// The note a click would create: the whole line segment under the pointer, else a short note on the pointer's row.

- (BOOL)isGhostVisible {
    ttr::NoteEvent n;
    return [self ghostNote:&n];
}

- (NSRect)ghostRect {
    ttr::NoteEvent n;
    return [self ghostNote:&n] ? [self rectForNote:n] : NSZeroRect;
}

- (NSString*)ghostLabel {
    ttr::NoteEvent n;
    if (![self ghostNote:&n]) return nil;
    const ttr::TimeGrid grid = _kernel->timeGrid();
    return [NSString stringWithFormat:@"%@ · %@", TTRNoteName(n.pitch), lengthName((n.endSec - n.startSec) * grid.beatsPerSec())];
}

@end
