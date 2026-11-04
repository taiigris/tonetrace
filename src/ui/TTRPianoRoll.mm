#import "ui/TTRPianoRoll.h"

#include <algorithm>
#include <cmath>

#include "core/Instruments.h"
#import "ui/TTRTheme.h"
#import "ui/TTRWaveLane.h"

static const uint32_t kRollBg = 0x17191D, kBlackRow = 0x141619, kOctaveLine = 0x2A2E35, kGridLine = 0x1E2126;
static const double kMaxNoteSec = 30.0;  // notes that start this long before the left edge may still reach into view

@implementation TTRPianoRoll_v1 {
    std::shared_ptr<TTRTimeline> _tl;
    ttr::DocumentPtr _doc;
    ttr::MixerState _mixer;
    double _playhead;
    NSPoint _hover;
    BOOL _hasHover;
    NSTrackingArea* _tracking;
}

- (instancetype)initWithTimeline:(std::shared_ptr<TTRTimeline>)timeline {
    self = [super initWithFrame:NSZeroRect];
    if (self) {
        _tl = std::move(timeline);
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (void)updateWithDocument:(ttr::DocumentPtr)doc mixer:(const ttr::MixerState&)mixer playhead:(double)playhead {
    _doc = std::move(doc);
    _mixer = mixer;
    _playhead = playhead;
    self.needsDisplay = YES;
}

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    if (_tracking) [self removeTrackingArea:_tracking];
    _tracking = [[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
                                               owner:self userInfo:nil];
    [self addTrackingArea:_tracking];
}

- (void)mouseMoved:(NSEvent*)event { _hover = [self convertPoint:event.locationInWindow fromView:nil]; _hasHover = YES; self.needsDisplay = YES; }
- (void)mouseExited:(NSEvent*)event { _hasHover = NO; self.needsDisplay = YES; }
- (void)scrollWheel:(NSEvent*)event { if (_onWheel) _onWheel(event, [self convertPoint:event.locationInWindow fromView:nil].x - kTTRKeyboardWidth); }
- (void)magnifyWithEvent:(NSEvent*)event { if (_onMagnify) _onMagnify(event, [self convertPoint:event.locationInWindow fromView:nil].x - kTTRKeyboardWidth); }

- (void)mouseDown:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (!_doc || p.x < kTTRKeyboardWidth || !_onSeek) return;
    _onSeek(std::clamp(_tl->timeForX(p.x - kTTRKeyboardWidth), 0.0, _doc->lengthSec));
}
- (void)mouseDragged:(NSEvent*)event { [self mouseDown:event]; }

- (CGFloat)yForPitch:(int)pitch { return CGFloat((ttr::kMaxNote - pitch) * _tl->rowH - _tl->scrollY); }
- (CGFloat)noteX:(double)sec { return kTTRKeyboardWidth + CGFloat(_tl->xForTime(sec)); }

- (void)drawRows:(NSRect)b {
    const CGFloat rowH = CGFloat(_tl->rowH);
    for (int pitch = ttr::kMaxNote; pitch >= ttr::kMinNote; --pitch) {
        const CGFloat y = [self yForPitch:pitch];
        if (y > b.size.height || y + rowH < 0) continue;
        if (TTRIsBlackKey(pitch)) {
            [TTRHex(kBlackRow) setFill];
            NSRectFill(NSMakeRect(kTTRKeyboardWidth, y, b.size.width - kTTRKeyboardWidth, rowH));
        }
        if (pitch % 12 == 0) {  // a line under every C
            [TTRHex(kOctaveLine) setFill];
            NSRectFill(NSMakeRect(kTTRKeyboardWidth, round(y + rowH) - 1, b.size.width - kTTRKeyboardWidth, 1));
        }
    }
}

- (void)drawTimeGrid:(NSRect)b {
    static const double kSteps[] = {0.25, 0.5, 1, 2, 5, 10, 15, 30, 60};
    double step = 120.0;
    for (double s : kSteps) if (s * _tl->pxPerSec >= 90.0) { step = s; break; }
    [TTRHex(kGridLine) setFill];
    for (double t = std::floor(std::max(0.0, _tl->scrollSec) / step) * step; ; t += step) {
        const CGFloat x = [self noteX:t];
        if (x > b.size.width) break;
        if (x >= kTTRKeyboardWidth) NSRectFill(NSMakeRect(round(x), 0, 1, b.size.height));
    }
}

- (void)drawPendingRegion:(NSRect)b {
    if (!_doc || !_doc->transcription.isActive()) return;
    const CGFloat x = std::max<CGFloat>(kTTRKeyboardWidth, [self noteX:_doc->transcription.finalizedSec]);
    const CGFloat end = _doc->lengthSec > 0.0 ? std::min<CGFloat>(b.size.width, [self noteX:_doc->lengthSec]) : b.size.width;
    if (x >= end) return;
    [TTRHex(0x000000, 0.35) setFill];
    NSRectFillUsingOperation(NSMakeRect(x, 0, end - x, b.size.height), NSCompositingOperationSourceOver);
    [TTRHex(0x5B8CFF, 0.7) setFill];
    NSRectFillUsingOperation(NSMakeRect(x, 0, 2, b.size.height), NSCompositingOperationSourceOver);
    NSString* label = _doc->transcription.phase == ttr::TranscriptionPhase::Loading ? @"Loading…" : @"Transcribing…";
    if (!_doc->transcription.message.empty()) label = [NSString stringWithUTF8String:_doc->transcription.message.c_str()];
    TTRDrawText(label, NSMakeRect(x + 12, 10, MAX(0.0, end - x - 20), 18), TTRFont(12, NSFontWeightSemibold), TTRHex(0x9DB8FF));
}

// The index of the hovered note (or -1) is found while drawing, the label is drawn last.
- (int)drawNotes:(NSRect)b {
    if (!_doc || _doc->notes.empty()) return -1;
    const std::vector<ttr::NoteEvent>& notes = _doc->notes;
    const double fromSec = _tl->timeForX(0) - kMaxNoteSec, toSec = _tl->timeForX(b.size.width - kTTRKeyboardWidth);
    const auto first = std::lower_bound(notes.begin(), notes.end(), fromSec, [](const ttr::NoteEvent& n, double t) { return n.startSec < t; });
    const CGFloat rowH = CGFloat(_tl->rowH), barH = std::max<CGFloat>(2.0, rowH - 2.0);
    int hovered = -1;
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, NSMakeRect(kTTRKeyboardWidth, 0, b.size.width - kTTRKeyboardWidth, b.size.height));
    for (auto it = first; it != notes.end() && it->startSec <= toSec; ++it) {
        const CGFloat x0 = [self noteX:it->startSec], x1 = [self noteX:it->endSec];
        if (x1 < kTTRKeyboardWidth) continue;
        const CGFloat y = [self yForPitch:it->pitch];
        if (y > b.size.height || y + rowH < 0) continue;
        const NSRect bar = NSMakeRect(round(x0), round(y + 1), std::max<CGFloat>(3.0, round(x1 - x0)), barH);
        const BOOL audible = _mixer.isAudible(it->program);
        TTRFillRounded(bar, std::min<CGFloat>(2.5, barH / 3), TTRHex(TTRInstrumentRGB(it->program), audible ? 0.92 : 0.18));
        if (_hasHover && NSPointInRect(_hover, NSInsetRect(bar, -1, -1))) hovered = int(it - notes.begin());
    }
    CGContextRestoreGState(ctx);
    return hovered;
}

- (void)drawKeyboard:(NSRect)b {
    [TTRHex(0xE9EBEE) setFill];
    NSRectFill(NSMakeRect(0, 0, kTTRKeyboardWidth, b.size.height));
    const CGFloat rowH = CGFloat(_tl->rowH);
    NSFont* font = TTRFont(std::min<CGFloat>(10, rowH - 1), NSFontWeightSemibold);
    for (int pitch = ttr::kMaxNote; pitch >= ttr::kMinNote; --pitch) {
        const CGFloat y = [self yForPitch:pitch];
        if (y > b.size.height || y + rowH < 0) continue;
        if (TTRIsBlackKey(pitch)) {
            [TTRHex(0x1B1D21) setFill];
            NSRectFill(NSMakeRect(0, y, kTTRKeyboardWidth * 0.62, rowH));
        } else if (pitch % 12 == 4 || pitch % 12 == 11) {  // the edge between E-F and B-C
            [TTRHex(0xB8BDC5) setFill];
            NSRectFill(NSMakeRect(0, round(y), kTTRKeyboardWidth, 1));
        }
        if (pitch % 12 == 0 && rowH >= 7)
            TTRDrawText(TTRNoteName(pitch), NSMakeRect(kTTRKeyboardWidth - 34, y, 30, rowH), font, TTRHex(0x6A707B), NSTextAlignmentRight);
    }
    [TTRHex(0x0E0F12) setFill];
    NSRectFill(NSMakeRect(kTTRKeyboardWidth - 1, 0, 1, b.size.height));
}

- (void)drawPlayhead:(NSRect)b {
    if (!_doc) return;
    const CGFloat x = round([self noteX:_playhead]) + 0.5;
    if (x < kTTRKeyboardWidth || x > b.size.width) return;
    [TTRHex(0xF2F4F7) setFill];
    NSRectFill(NSMakeRect(x - 0.5, 0, 1.5, b.size.height));
}

- (void)drawHoverLabel:(int)index {
    if (index < 0 || !_doc || size_t(index) >= _doc->notes.size()) return;
    const ttr::NoteEvent& n = _doc->notes[size_t(index)];
    NSString* text = [NSString stringWithFormat:@"%@ · %s", TTRNoteName(n.pitch), ttr::instrumentName(n.program).c_str()];
    if (n.program == ttr::kDrumProgram) text = [NSString stringWithFormat:@"Drums · key %d", n.pitch];
    NSFont* font = TTRFont(11, NSFontWeightSemibold);
    const CGFloat w = TTRTextWidth(text, font) + 14, h = 20;
    NSRect pill = NSMakeRect(_hover.x + 12, _hover.y - h - 6, w, h);
    if (NSMaxX(pill) > self.bounds.size.width - 4) pill.origin.x = _hover.x - 12 - w;
    if (pill.origin.y < 2) pill.origin.y = _hover.y + 14;
    TTRFillRounded(pill, 5, TTRHex(0x08090B, 0.9));
    TTRStrokeRounded(pill, 5, TTRHex(TTRInstrumentRGB(n.program), 0.8), 1.0);
    TTRDrawText(text, NSInsetRect(pill, 7, 0), font, TTR_TEXT);
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    [TTRHex(kRollBg) setFill];
    NSRectFill(b);
    [self drawRows:b];
    [self drawTimeGrid:b];
    const int hovered = [self drawNotes:b];
    [self drawPendingRegion:b];
    [self drawPlayhead:b];
    [self drawKeyboard:b];
    [self drawHoverLabel:hovered];
}

@end
