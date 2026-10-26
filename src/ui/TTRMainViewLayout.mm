// Layout and drawing of the main view: waveform lane + map + piano on the left, a compact column of controls on the right.
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

static const CGFloat kCompactHeight = 520, kRelaxedHeight = 640;  // the panel scales between these window heights
static const CGFloat kPanelPad = 10, kColumnGap = 6;

static CGFloat lerp(CGFloat compact, CGFloat relaxed, CGFloat t) { return round(compact + (relaxed - compact) * t); }

@implementation TTRMainView_v1 (Layout)

- (void)layoutControls {
    const CGFloat W = self.bounds.size.width, H = self.bounds.size.height;
    const CGFloat leftW = W - kTTRPanelWidth;
    const CGFloat t = MIN(1.0, MAX(0.0, (H - kCompactHeight) / (kRelaxedHeight - kCompactHeight)));

    // left: lane, map, scroll bar, piano
    const CGFloat laneH = lerp(56, 70, t), waveH = kTTRRulerHeight + laneH;
    const CGFloat mapX = kTTRMargin + kTTRPianoWidth, mapW = MAX(60, leftW - mapX - kTTRMargin);
    const CGFloat mapY = kTTRMargin + waveH + 4;
    const CGFloat scrollY = H - kTTRStatusHeight - kTTRScrollHeight - 5;
    _wave.frame = NSMakeRect(mapX, kTTRMargin, mapW, waveH);
    _map.frame = NSMakeRect(mapX, mapY, mapW, MAX(60, scrollY - 4 - mapY));
    _piano.frame = NSMakeRect(kTTRMargin, mapY, kTTRPianoWidth, _map.frame.size.height);
    _scroll.frame = NSMakeRect(mapX, scrollY, mapW, kTTRScrollHeight);

    // right panel, top to bottom
    const CGFloat x = leftW + kPanelPad, w = kTTRPanelWidth - 2 * kPanelPad, half = floor((w - kColumnGap) / 2), gap = lerp(3, 5, t);
    const CGFloat logoH = lerp(30, 38, t), knobH = lerp(46, 58, t), btnH = lerp(22, 26, t), recH = lerp(46, 60, t), dragH = lerp(26, 32, t), readH = lerp(32, 36, t);
    CGFloat y = 8 + logoH + gap;
    const CGFloat col2 = x + half + kColumnGap;

    _gainKnob.frame = NSMakeRect(x, y, half, knobH);
    _rangeKnob.frame = NSMakeRect(col2, y, half, knobH);
    y += knobH + gap;
    const CGFloat stepW = 28;
    _speedMinus.frame = NSMakeRect(x, y, stepW, btnH);
    _speedReadout.frame = NSMakeRect(x + stepW + 4, y, w - 2 * (stepW + 4), btnH);
    _speedPlus.frame = NSMakeRect(x + w - stepW, y, stepW, btnH);
    y += btnH + gap;
    _hideDrumsButton.frame = NSMakeRect(x, y, half, btnH);
    _magnetButton.frame = NSMakeRect(col2, y, half, btnH);
    y += btnH + gap;
    _snap.frame = NSMakeRect(x, y, w, btnH);
    y += btnH + gap;
    _quickGrabKnob.frame = NSMakeRect(x, y, half, knobH);
    _grabButton.frame = NSMakeRect(col2, y + (knobH - btnH) / 2 - 3, half, btnH);
    y += knobH + gap;
    _recordButton.frame = NSMakeRect(x + (w - recH * 1.6) / 2, y, recH * 1.6, recH);
    y += recH + gap;
    _resetButton.frame = NSMakeRect(x, y, half, btnH);
    _clearButton.frame = NSMakeRect(col2, y, half, btnH);
    y += btnH + gap;
    _previewButton.frame = NSMakeRect(x, y, half, btnH);
    _soloButton.frame = NSMakeRect(col2, y, half, btnH);
    y += btnH + gap;
    _soundKnob.frame = NSMakeRect(x, y, half, knobH);
    _mixKnob.frame = NSMakeRect(col2, y, half, knobH);
    y += knobH + gap;
    _dragButton.frame = NSMakeRect(x, y, w, dragH);
    y += dragH + gap;
    _saveButton.frame = NSMakeRect(x, y, half, btnH);
    _syncButton.frame = NSMakeRect(col2, y, half, btnH);
    y += btnH + gap;
    _keyReadout.frame = NSMakeRect(x, y, half, readH);
    _bpmReadout.frame = NSMakeRect(col2, y, half, readH);
    y += readH + gap;
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

    // label in the corner left of the waveform lane
    const NSRect wave = _wave.frame;
    TTRDrawText(@"WAVE", NSMakeRect(kTTRMargin, wave.origin.y + kTTRRulerHeight, kTTRPianoWidth, wave.size.height - kTTRRulerHeight), TTRFont(9, NSFontWeightSemibold), TTRHex(0x5E6675), NSTextAlignmentCenter, 1.0);

    // status line
    const NSRect status = NSMakeRect(0, b.size.height - kTTRStatusHeight, leftW, kTTRStatusHeight);
    [TTRHex(0x0F1115) setFill];
    NSRectFill(status);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, status.origin.y, leftW, 1));
    [self drawStatusBar:status];
}

@end
