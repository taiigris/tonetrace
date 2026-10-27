// Layout and drawing of the main view: waveform lane + map + piano on the left, a compact column of controls on the right.
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

static const CGFloat kCompactHeight = 650, kRelaxedHeight = 800;  // the panel scales between these window heights
static const CGFloat kPanelPad = 10, kColumnGap = 6;

static CGFloat lerp(CGFloat compact, CGFloat relaxed, CGFloat t) { return round(compact + (relaxed - compact) * t); }

@implementation TTRMainView_v1 (Layout)

- (void)layoutControls {
    const CGFloat W = self.bounds.size.width, H = self.bounds.size.height;
    const CGFloat leftW = W - kTTRPanelWidth;
    const CGFloat t = MIN(1.0, MAX(0.0, (H - kCompactHeight) / (kRelaxedHeight - kCompactHeight)));

    // left: lane, map, scroll bar, piano
    const CGFloat laneH = _showWave ? kTTRWaveLaneHeight : 0, waveH = kTTRRulerHeight + laneH;
    const CGFloat mapX = kTTRMargin + kTTRPianoWidth, mapW = MAX(60, leftW - mapX - kTTRMargin);
    const CGFloat mapY = kTTRMargin + waveH + 4;
    const CGFloat scrollY = H - kTTRStatusHeight - kTTRScrollHeight - 5;
    _wave.frame = NSMakeRect(mapX, kTTRMargin, mapW, waveH);
    _map.frame = NSMakeRect(mapX, mapY, mapW, MAX(60, scrollY - 4 - mapY));
    _piano.frame = NSMakeRect(kTTRMargin, mapY, kTTRPianoWidth, _map.frame.size.height);
    _scroll.frame = NSMakeRect(mapX, scrollY, mapW, kTTRScrollHeight);

    // right panel, top to bottom
    const CGFloat x = leftW + kPanelPad, w = kTTRPanelWidth - 2 * kPanelPad, half = floor((w - kColumnGap) / 2), gap = lerp(2, 4, t);
    const CGFloat logoH = lerp(30, 38, t), knobH = lerp(42, 52, t), btnH = lerp(20, 24, t), recH = lerp(40, 52, t), dragH = lerp(24, 32, t), readH = lerp(34, 36, t);
    CGFloat y = 8 + logoH + gap;
    const CGFloat col2 = x + half + kColumnGap;

    // each helper places one row and moves y down
    auto pair = [&](NSView* left, NSView* right, CGFloat h) { left.frame = NSMakeRect(x, y, half, h); right.frame = NSMakeRect(col2, y, half, h); y += h + gap; };
    auto full = [&](NSView* v, CGFloat h) { v.frame = NSMakeRect(x, y, w, h); y += h + gap; };
    auto stepper = [&](NSView* minus, NSView* readout, NSView* plus, CGFloat stepW) {
        minus.frame = NSMakeRect(x, y, stepW, btnH);
        readout.frame = NSMakeRect(x + stepW + 3, y, w - 2 * (stepW + 3), btnH);
        plus.frame = NSMakeRect(x + w - stepW, y, stepW, btnH);
        y += btnH + gap;
    };
    pair(_gainKnob, _rangeKnob, knobH);
    stepper(_speedMinus, _speedReadout, _speedPlus, 28);
    pair(_spectrumButton, _linesButton, btnH);
    pair(_hideDrumsButton, _fundamentalsButton, btnH);
    pair(_magnetButton, _grabOnClickButton, btnH);
    pair(_alignButton, _waveButton, btnH);
    stepper(_tuningMinus, _tuningReadout, _tuningPlus, 24);
    full(_snap, btnH);
    _quickGrabKnob.frame = NSMakeRect(x, y, half, knobH);
    _grabButton.frame = NSMakeRect(col2, y + (knobH - btnH) / 2, half, btnH);
    y += knobH + gap;
    _recordButton.frame = NSMakeRect(x + (w - recH * 1.6) / 2, y, recH * 1.6, recH);
    y += recH + gap;
    pair(_resetButton, _clearButton, btnH);
    pair(_previewButton, _soloButton, btnH);
    pair(_soundKnob, _mixKnob, knobH);
    full(_dragButton, dragH);
    pair(_saveButton, _syncButton, btnH);
    full(_samplerButton, btnH);
    full(_samplerDragButton, btnH);
    full(_bridgeReadout, btnH);
    pair(_keyReadout, _bpmReadout, readH);
    _info.frame = NSMakeRect(x, y, w, MAX(40, H - y - 8));
    _infoButton.frame = NSMakeRect(leftW + kTTRPanelWidth - kPanelPad - 22, 8 + (logoH - 22) / 2, 22, 22);
    _logoView.frame = NSMakeRect(x, 8, w - 22 - kColumnGap, logoH);
    [self layoutStatusButtons];
}

// ------------------------------------------------------------ drawing

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    const CGFloat leftW = b.size.width - kTTRPanelWidth;
    [TTR_BG setFill];
    NSRectFill(b);
    [TTR_PANEL setFill];
    NSRectFill(NSMakeRect(leftW, 0, kTTRPanelWidth, b.size.height));
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(leftW, 0, 1, b.size.height));

    // status line
    const NSRect status = NSMakeRect(0, b.size.height - kTTRStatusHeight, leftW, kTTRStatusHeight);
    [TTRHex(0x0F1115) setFill];
    NSRectFill(status);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, status.origin.y, leftW, 1));
    [self drawStatusBar:status];
}

@end
