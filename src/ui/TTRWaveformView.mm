#import "ui/TTRWaveformView.h"

#import <QuartzCore/QuartzCore.h>

#import "ui/TTRTheme.h"
#import "ui/TTRViewportInput.h"

const CGFloat kTTRRulerHeight = 24.0;

static const CGFloat kLoopDragPixels = 3.0, kMinBeatPixels = 7.0, kMinBarPixels = 10.0, kLabelMinPixels = 46.0, kSecondLabelPixels = 56.0;
static const double kWaveFill = 0.9;

static void fillRect(CGContextRef ctx, CGRect r, uint32_t rgb, CGFloat alpha) {
    CGContextSetRGBFillColor(ctx, ((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, alpha);
    CGContextFillRect(ctx, r);
}

@implementation TTRWaveformView_v1 {
    std::shared_ptr<ttr::TonetraceKernel> _kernel;
    std::shared_ptr<TTRViewport> _vp;
    ttr::MapView _mv;
    ttr::KernelStatus _status;
    double _recorded, _playhead, _pressSec;
    ttr::TimeGrid _grid;
    NSPoint _pressPoint;
    BOOL _pressing, _dragging;
}

- (instancetype)initWithKernel:(std::shared_ptr<ttr::TonetraceKernel>)kernel viewport:(std::shared_ptr<TTRViewport>)viewport {
    self = [super initWithFrame:NSMakeRect(0, 0, 700, 94)];
    if (self) {
        _kernel = std::move(kernel);
        _vp = std::move(viewport);
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
        self.toolTip = @"Click to set where the preview starts, drag to loop a region";
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)isOpaque { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }

- (NSRect)laneRect { return NSMakeRect(0, kTTRRulerHeight, self.bounds.size.width, MAX(0, self.bounds.size.height - kTTRRulerHeight)); }
- (NSPoint)pointForTime:(double)seconds { const NSRect l = [self laneRect]; return NSMakePoint(_vp->xForTime(seconds), NSMidY(l)); }

- (void)updateWithMapView:(const ttr::MapView&)view status:(ttr::KernelStatus)status recorded:(double)recorded playhead:(double)playhead {
    _mv = view;
    _status = status;
    _recorded = recorded;
    _playhead = playhead;
    _grid = _kernel->timeGrid();
    self.needsDisplay = YES;
}

// ------------------------------------------------------------ mouse

- (double)timeAtEvent:(NSEvent*)e {
    return MAX(0.0, _vp->timeForX([self convertPoint:e.locationInWindow fromView:nil].x));
}

- (void)mouseDown:(NSEvent*)event {
    _pressing = YES;
    _dragging = NO;
    _pressPoint = [self convertPoint:event.locationInWindow fromView:nil];
    _pressSec = [self timeAtEvent:event];
}

- (void)mouseDragged:(NSEvent*)event {
    if (!_pressing) return;
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (!_dragging && fabs(p.x - _pressPoint.x) < kLoopDragPixels) return;
    _dragging = YES;
    const double t = [self timeAtEvent:event];
    _kernel->setPreviewLoop(MIN(_pressSec, t), MAX(_pressSec, t));
    self.needsDisplay = YES;
}

- (void)mouseUp:(NSEvent*)event {
    if (_pressing && !_dragging) {  // a click moves the start and drops the loop
        _kernel->setPreviewStart(_pressSec);
        _kernel->setPreviewLoop(0.0, 0.0);
    }
    _pressing = _dragging = NO;
    self.needsDisplay = YES;
}

- (void)mouseMoved:(NSEvent*)event {
    _vp->hasCursor = YES;
    _vp->cursorSec = _vp->timeForX([self convertPoint:event.locationInWindow fromView:nil].x);
    if (_onViewportChange) _onViewportChange();
}

- (void)mouseExited:(NSEvent*)event { _vp->hasCursor = NO; if (_onViewportChange) _onViewportChange(); }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
                                                       options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                                                         owner:self userInfo:nil]];
}

- (void)scrollWheel:(NSEvent*)event {
    __weak TTRWaveformView_v1* weak = self;
    NSPoint anchor = [self convertPoint:event.locationInWindow fromView:nil];
    TTRHandleWheel(event, _vp.get(), anchor, NSMakeSize(self.bounds.size.width, 0), NO, ^(double speed) { if (weak.onZoom) weak.onZoom(speed); });
    if (_onViewportChange) _onViewportChange();
}

- (void)magnifyWithEvent:(NSEvent*)event {
    __weak TTRWaveformView_v1* weak = self;
    TTRHandlePinch(event, _vp.get(), [self convertPoint:event.locationInWindow fromView:nil], NSMakeSize(self.bounds.size.width, 0), ^(double speed) { if (weak.onZoom) weak.onZoom(speed); });
    if (_onViewportChange) _onViewportChange();
}

// ------------------------------------------------------------ drawing

- (void)drawRect:(NSRect)dirty {
    CGContextRef ctx = [NSGraphicsContext currentContext].CGContext;
    CGContextClipToRect(ctx, self.bounds);
    const NSRect lane = [self laneRect];
    fillRect(ctx, lane, 0x0F1217, 1.0);
    CGContextSaveGState(ctx);
    CGContextClipToRect(ctx, lane);
    [self drawBeatGrid:ctx lane:lane];
    [self drawLoop:ctx lane:lane];
    [self drawWave:ctx lane:lane];
    [self drawMarkers:ctx lane:lane];
    CGContextRestoreGState(ctx);
    [self drawRuler:ctx];
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, self.bounds.size.height - 1, self.bounds.size.width, 1));
}

- (void)drawBeatGrid:(CGContextRef)ctx lane:(NSRect)lane {
    const double bps = _grid.beatsPerSec(), ppq0 = _grid.startPpq, beatPx = _vp->pxPerSec / bps;
    if (beatPx < kMinBeatPixels) return;
    const double t0 = _vp->timeForX(0), t1 = _vp->timeForX(lane.size.width);
    for (long k = long(floor(ppq0 + t0 * bps)); k <= long(ceil(ppq0 + t1 * bps)); ++k) {
        const BOOL isBar = k % 4 == 0;
        if (isBar && beatPx * 4 < kMinBarPixels) continue;
        fillRect(ctx, CGRectMake(floor(_vp->xForTime((k - ppq0) / bps)) + 0.5, lane.origin.y, 1, lane.size.height), 0xFFFFFF, isBar ? 0.12 : 0.04);
    }
}

- (void)drawWave:(CGContextRef)ctx lane:(NSRect)lane {
    const CGFloat mid = NSMidY(lane), half = lane.size.height / 2 * kWaveFill;
    fillRect(ctx, CGRectMake(0, floor(mid), lane.size.width, 1), 0xFFFFFF, 0.08);
    if (!_mv.wave || _mv.waveCount <= 0) return;
    NSBezierPath* path = [NSBezierPath bezierPath];
    for (CGFloat x = 0; x < lane.size.width; x += 1.0) {
        const double t0 = _vp->timeForX(x), t1 = _vp->timeForX(x + 1);
        const int a = int(floor(t0 * ttr::kPeaksPerSecond)), b = MAX(a, int(ceil(t1 * ttr::kPeaksPerSecond)) - 1);
        if (b < 0 || a >= _mv.waveCount) continue;
        float lo = 1.0f, hi = -1.0f;
        for (int i = MAX(a, 0); i <= MIN(b, _mv.waveCount - 1); ++i) {
            lo = MIN(lo, _mv.wave[size_t(i) * 2]);
            hi = MAX(hi, _mv.wave[size_t(i) * 2 + 1]);
        }
        const CGFloat y0 = mid - MAX(-1.0f, MIN(1.0f, hi)) * half, y1 = mid - MAX(-1.0f, MIN(1.0f, lo)) * half;
        [path moveToPoint:NSMakePoint(x + 0.5, y0)];
        [path lineToPoint:NSMakePoint(x + 0.5, MAX(y1, y0 + 1))];
    }
    path.lineWidth = 1.0;
    [TTRHex(0x2EE6C5, 0.85) setStroke];
    [path stroke];
}

- (void)drawLoop:(CGContextRef)ctx lane:(NSRect)lane {
    double a = 0, b = 0;
    if (!_kernel->previewLoop(&a, &b)) return;
    const CGFloat x0 = _vp->xForTime(a), x1 = _vp->xForTime(b);
    fillRect(ctx, CGRectMake(x0, lane.origin.y, x1 - x0, lane.size.height), 0x2EE6C5, 0.14);
    fillRect(ctx, CGRectMake(x0, lane.origin.y, 1.5, lane.size.height), 0x2EE6C5, 0.9);
    fillRect(ctx, CGRectMake(x1 - 1.5, lane.origin.y, 1.5, lane.size.height), 0x2EE6C5, 0.9);
}

- (void)drawMarkers:(CGContextRef)ctx lane:(NSRect)lane {
    const CGFloat startX = floor(_vp->xForTime(_kernel->previewStart())) + 0.5;
    if (_kernel->previewStart() > 0.0) {
        fillRect(ctx, CGRectMake(startX, lane.origin.y, 1, lane.size.height), 0xFFFFFF, 0.5);
        NSBezierPath* flag = [NSBezierPath bezierPath];
        [flag moveToPoint:NSMakePoint(startX, lane.origin.y)];
        [flag lineToPoint:NSMakePoint(startX + 8, lane.origin.y)];
        [flag lineToPoint:NSMakePoint(startX, lane.origin.y + 8)];
        [flag closePath];
        [TTRHex(0xFFFFFF, 0.8) setFill];
        [flag fill];
    }
    if (_status == ttr::KernelStatus::Recording) {
        const CGFloat x = floor(_vp->xForTime(_recorded)) + 0.5;
        fillRect(ctx, CGRectMake(x, lane.origin.y, 1, lane.size.height), 0xFF4D5E, 0.95);
    } else if (_kernel->previewing()) {
        const CGFloat x = floor(_vp->xForTime(_playhead)) + 0.5;
        fillRect(ctx, CGRectMake(x, lane.origin.y, 1.5, lane.size.height), 0xFFFFFF, 0.9);
    }
    if (_vp->hasCursor) fillRect(ctx, CGRectMake(floor(_vp->xForTime(_vp->cursorSec)) + 0.5, lane.origin.y, 1, lane.size.height), 0xFFFFFF, 0.5);
}

- (void)drawRuler:(CGContextRef)ctx {
    const CGFloat h = kTTRRulerHeight, w = self.bounds.size.width;
    fillRect(ctx, CGRectMake(0, 0, w, h), 0x1B1F26, 1.0);
    fillRect(ctx, CGRectMake(0, h - 1, w, 1), 0x2C333E, 1.0);

    static const double steps[] = {0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120};  // seconds row (bottom)
    double step = 120;
    for (double s : steps) if (s * _vp->pxPerSec >= kSecondLabelPixels) { step = s; break; }
    NSDictionary* secAttrs = TTRTextAttrs(TTRMono(9, NSFontWeightRegular), TTR_DIM);
    const double t0 = _vp->timeForX(0), t1 = _vp->timeForX(w);
    for (long i = long(floor(t0 / step)); i * step <= t1; ++i) {
        const double t = i * step;
        if (t < 0) continue;
        const CGFloat x = floor(_vp->xForTime(t)) + 0.5;
        fillRect(ctx, CGRectMake(x, h - 6, 1, 5), 0x8A93A3, 0.6);
        const int total = int(t + 1e-6);
        NSString* label = step < 1.0 ? [NSString stringWithFormat:@"%d:%04.1f", int(t) / 60, fmod(t, 60.0)] : [NSString stringWithFormat:@"%d:%02d", total / 60, total % 60];
        [label drawAtPoint:NSMakePoint(x + 4, h - 13) withAttributes:secAttrs];
    }

    const double bps = _grid.beatsPerSec(), ppq0 = _grid.startPpq, barPx = _vp->pxPerSec / bps * 4.0;  // bar row (top)
    long every = 1;
    while (barPx * every < kLabelMinPixels && every < 1024) every *= 2;
    NSDictionary* barAttrs = TTRTextAttrs(TTRMono(9, NSFontWeightSemibold), TTRHex(0x2EE6C5, 0.85));
    for (long bar = long(floor((ppq0 + t0 * bps) / 4.0)); bar <= long(ceil((ppq0 + t1 * bps) / 4.0)); ++bar) {
        if (bar % every != 0) continue;
        const CGFloat x = floor(_vp->xForTime((bar * 4.0 - ppq0) / bps)) + 0.5;
        fillRect(ctx, CGRectMake(x, 2, 1, 9), 0x2EE6C5, 0.55);
        [[NSString stringWithFormat:@"%ld", bar + 1] drawAtPoint:NSMakePoint(x + 4, 1) withAttributes:barAttrs];
    }
}

@end
