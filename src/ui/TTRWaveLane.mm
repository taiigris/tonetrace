#import "ui/TTRWaveLane.h"

#include <algorithm>
#include <cmath>

#import "ui/TTRTheme.h"

static const CGFloat kBarStep = 3.0, kBarWidth = 2.0, kTopPad = 10.0;
static const uint32_t kBarColor = 0x8C96AA, kPendingBarColor = 0x4A5060, kRecordBarColor = 0xE0606C, kLaneBg = 0x181A20;

// Seconds between ruler labels so that labels are at least `minPx` apart.
static double rulerStep(double pxPerSec, double minPx) {
    static const double kSteps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300};
    for (double s : kSteps)
        if (s * pxPerSec >= minPx) return s;
    return 600.0;
}

static NSString* rulerLabel(double sec, double step) {
    const int minutes = int(sec) / 60;
    if (step < 1.0) return [NSString stringWithFormat:@"%d:%04.1f", minutes, sec - minutes * 60.0];
    return [NSString stringWithFormat:@"%d:%02d", minutes, int(std::lround(sec)) % 60];
}

@implementation TTRWaveLane_v1 {
    std::shared_ptr<TTRTimeline> _tl;
    ttr::DocumentPtr _doc;
    ttr::RecordingView _rec;
    BOOL _isRecording;
    double _playhead;
    BOOL _seeking;
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

- (void)updateWithDocument:(ttr::DocumentPtr)doc recording:(const ttr::RecordingView&)recording isRecording:(BOOL)isRecording playhead:(double)playhead {
    _doc = std::move(doc);
    _rec = recording;
    _isRecording = isRecording;
    _playhead = playhead;
    self.needsDisplay = YES;
}

- (CGFloat)noteX:(double)sec { return kTTRKeyboardWidth + CGFloat(_tl->xForTime(sec)); }

- (void)seekAt:(NSPoint)p {
    if (!_doc || _doc->lengthSec <= 0.0 || !_onSeek) return;
    _onSeek(std::clamp(_tl->timeForX(p.x - kTTRKeyboardWidth), 0.0, _doc->lengthSec));
}

- (void)mouseDown:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (p.x < kTTRKeyboardWidth) return;
    _seeking = YES;
    [self seekAt:p];
}
- (void)mouseDragged:(NSEvent*)event { if (_seeking) [self seekAt:[self convertPoint:event.locationInWindow fromView:nil]]; }
- (void)mouseUp:(NSEvent*)event { _seeking = NO; }
- (void)scrollWheel:(NSEvent*)event {
    if (_onWheel) _onWheel(event, [self convertPoint:event.locationInWindow fromView:nil].x - kTTRKeyboardWidth);
}

// min/max of the peaks between two times (pairs per 1/kPeaksPerSecond).
static bool rangeOf(const float* minMax, int count, double from, double to, float* lo, float* hi) {
    const int a = std::max(0, int(from * ttr::kPeaksPerSecond)), b = std::min(count, int(std::ceil(to * ttr::kPeaksPerSecond)));
    if (b <= a) return false;
    *lo = 0.0f;
    *hi = 0.0f;
    for (int i = a; i < b; ++i) {
        *lo = std::min(*lo, minMax[2 * i]);
        *hi = std::max(*hi, minMax[2 * i + 1]);
    }
    return true;
}

- (void)drawAxis:(NSRect)wave {
    NSFont* font = TTRMono(10, NSFontWeightRegular);
    TTRDrawText(@"+1.0", NSMakeRect(0, wave.origin.y, kTTRKeyboardWidth - 10, 14), font, TTRHex(0x5C6370), NSTextAlignmentRight);
    TTRDrawText(@"0", NSMakeRect(0, NSMidY(wave) - 7, kTTRKeyboardWidth - 10, 14), font, TTRHex(0x5C6370), NSTextAlignmentRight);
    TTRDrawText(@"-1.0", NSMakeRect(0, NSMaxY(wave) - 14, kTTRKeyboardWidth - 10, 14), font, TTRHex(0x5C6370), NSTextAlignmentRight);
}

- (void)drawBars:(NSRect)wave {
    const float* minMax = nullptr;
    int count = 0;
    BOOL live = NO;
    if (_isRecording && _rec.minMax && _rec.count > 0) {
        minMax = _rec.minMax;
        count = _rec.count;
        live = YES;
    } else if (_doc && _doc->peaks && _doc->peaks->count() > 0) {
        minMax = _doc->peaks->minMax.data();
        count = _doc->peaks->count();
    }
    if (!minMax) return;
    const BOOL transcribing = _doc && _doc->transcription.isActive() && !live;
    const double finalized = _doc ? _doc->transcription.finalizedSec : 0.0;
    const CGFloat mid = NSMidY(wave), half = wave.size.height / 2.0 - 2;
    for (CGFloat x = 0; x < wave.size.width; x += kBarStep) {
        const double t0 = _tl->timeForX(x), t1 = _tl->timeForX(x + kBarStep);
        float lo, hi;
        if (t1 < 0.0 || !rangeOf(minMax, count, t0, t1, &lo, &hi)) continue;
        const CGFloat top = mid - CGFloat(std::min(1.0f, hi)) * half, bottom = mid - CGFloat(std::max(-1.0f, lo)) * half;
        const uint32_t color = live ? kRecordBarColor : (transcribing && t0 >= finalized ? kPendingBarColor : kBarColor);
        [TTRHex(color) setFill];
        NSRectFill(NSMakeRect(kTTRKeyboardWidth + x, top, kBarWidth, std::max<CGFloat>(1.0, bottom - top)));
    }
}

- (void)drawRuler:(NSRect)ruler {
    [TTRHex(0x14161A) setFill];
    NSRectFill(ruler);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, ruler.origin.y, ruler.size.width, 1));
    const double step = rulerStep(_tl->pxPerSec, 90.0);
    const double first = std::floor(std::max(0.0, _tl->scrollSec) / step) * step;
    NSFont* font = TTRMono(11, NSFontWeightRegular);
    for (double t = first; ; t += step) {
        const CGFloat x = [self noteX:t];
        if (x > ruler.size.width) break;
        if (x < kTTRKeyboardWidth - 1) continue;
        [TTRHex(0x2C3038) setFill];
        NSRectFill(NSMakeRect(round(x), ruler.origin.y + 6, 1, ruler.size.height - 6));
        TTRDrawText(rulerLabel(t, step), NSMakeRect(x + 6, ruler.origin.y, 80, ruler.size.height), font, TTRHex(0x6F7684));
    }
}

- (void)drawPlayhead:(NSRect)b {
    if (!_doc || _isRecording) return;
    const CGFloat x = round([self noteX:_playhead]) + 0.5;
    if (x < kTTRKeyboardWidth || x > b.size.width) return;
    [TTRHex(0xF2F4F7) setFill];
    NSRectFill(NSMakeRect(x - 0.5, 0, 1.5, b.size.height));
    NSBezierPath* tri = [NSBezierPath bezierPath];
    [tri moveToPoint:NSMakePoint(x - 6, 0)];
    [tri lineToPoint:NSMakePoint(x + 6, 0)];
    [tri lineToPoint:NSMakePoint(x, 9)];
    [tri closePath];
    [tri fill];
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    [TTRHex(kLaneBg) setFill];
    NSRectFill(b);
    const NSRect ruler = NSMakeRect(0, b.size.height - kTTRRulerHeight, b.size.width, kTTRRulerHeight);
    const NSRect wave = NSMakeRect(kTTRKeyboardWidth, kTopPad, b.size.width - kTTRKeyboardWidth, ruler.origin.y - kTopPad - 6);
    [TTRHex(0x14161A) setFill];
    NSRectFill(NSMakeRect(0, 0, kTTRKeyboardWidth, ruler.origin.y));
    [self drawAxis:wave];
    [TTRHex(0x23262D) setFill];
    NSRectFill(NSMakeRect(kTTRKeyboardWidth, round(NSMidY(wave)), wave.size.width, 1));
    [self drawBars:wave];
    [self drawRuler:ruler];
    [self drawPlayhead:b];
}

@end
