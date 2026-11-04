// Layout and drawing of the main view (1280 x 720 by default, 1024 x 600 at the minimum): a 48 pt header across the top,
// piano + big map (with a compact waveform lane above it) on the left and a 200 pt column of controls on the right.
// Everything sits on an 8 pt grid; the options popover floats below the header.
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

static const CGFloat kGrid = 8, kPad = 16, kButtonH = 32, kSmallH = 24, kKnobH = 64, kRecordH = 80, kDragH = 40, kInfoMinH = 56, kInfoMaxH = 72;
static const CGFloat kSliderH = 48, kGrabW = 48, kStepW = 32, kIconW = 32, kSnapW = 104, kLogoW = 168;
static const CGFloat kOptionsW = 288, kOptionsRowH = 28, kOptionsPad = 16;

@implementation TTRMainView_v1 (Layout)

- (void)layoutHeader:(CGFloat)W {
    const CGFloat y = (kTTRHeaderHeight - kIconW) / 2;
    CGFloat x = W - kPad - kIconW;
    _infoButton.frame = NSMakeRect(x, y, kIconW, kIconW);
    x -= kGrid + kIconW;
    _optionsButton.frame = NSMakeRect(x, y, kIconW, kIconW);
    x -= kGrid + kIconW;
    _viewButton.frame = NSMakeRect(x, y, kIconW, kIconW);
    x -= kGrid + kSnapW;
    _snap.frame = NSMakeRect(x, y, kSnapW, kIconW);
    x -= kGrid + kIconW;
    _helpButton.frame = NSMakeRect(x, y, kIconW, kIconW);
    _logoView.frame = NSMakeRect(kPad, y, kLogoW, kIconW);
    _headerClip = NSMakeRect(kPad + kLogoW + kPad, 0, MAX(0.0, x - kPad - (kPad + kLogoW + kPad)), kTTRHeaderHeight);
}

// Right column: knobs, speed, quick grab, record, reset / preview from the top; the highlighted Drag MIDI and the small actions from the bottom.
- (void)layoutSidebar:(CGFloat)W height:(CGFloat)H {
    const CGFloat x = W - kTTRPanelWidth + kPad, w = kTTRPanelWidth - 2 * kPad, half = (w - kGrid) / 2;
    CGFloat y = kTTRHeaderHeight + kPad;
    _gainKnob.frame = NSMakeRect(x, y, half, kKnobH);
    _rangeKnob.frame = NSMakeRect(x + half + kGrid, y, half, kKnobH);
    y += kKnobH + kGrid;
    _speedMinus.frame = NSMakeRect(x, y, kStepW, kButtonH);
    _speedReadout.frame = NSMakeRect(x + kStepW + kGrid, y, w - 2 * (kStepW + kGrid), kButtonH);
    _speedPlus.frame = NSMakeRect(x + w - kStepW, y, kStepW, kButtonH);
    y += kButtonH + kGrid;
    _quickGrabSlider.frame = NSMakeRect(x, y, w - kGrabW - kGrid, kSliderH);
    _grabButton.frame = NSMakeRect(x + w - kGrabW, y + kSliderH - kButtonH, kGrabW, kButtonH);
    y += kSliderH + kGrid;
    _recordButton.frame = NSMakeRect(x, y, w, kRecordH);
    y += kRecordH + kGrid;
    _resetButton.frame = NSMakeRect(x, y, half, kButtonH);
    _previewButton.frame = NSMakeRect(x + half + kGrid, y, half, kButtonH);
    y += kButtonH + kGrid;
    _loadSongButton.frame = NSMakeRect(x, y, w, kButtonH);
    y += kButtonH + kGrid;

    CGFloat bottom = H - kPad;  // bottom group, built upwards
    auto upward = [&](NSView* v, CGFloat h, CGFloat gap) { bottom -= h; v.frame = NSMakeRect(x, bottom, w, h); bottom -= gap; };
    _undoButton.frame = NSMakeRect(x, bottom - kSmallH, half, kSmallH);
    _redoButton.frame = NSMakeRect(x + half + kGrid, bottom - kSmallH, half, kSmallH);
    bottom -= kSmallH + 4;
    upward(_samplerButton, kSmallH, 4);
    upward(_saveButton, kSmallH, kGrid);
    upward(_dragButton, kDragH, kGrid);
    const CGFloat free = bottom - y;
    const CGFloat infoH = MIN(kInfoMaxH, MAX(0.0, free));
    _info.frame = NSMakeRect(x, y, w, infoH);
    _info.hidden = infoH < kInfoMinH;
}

// The options popover: below the header, right-aligned. Returns nothing; subviews live in its own coordinates.
- (void)layoutOptions:(CGFloat)W {
    const CGFloat w = kOptionsW - 2 * kOptionsPad, half = (w - kGrid) / 2, third = (w - 2 * kGrid) / 3;
    CGFloat y = kOptionsPad;
    auto pair = [&](NSView* l, NSView* r) { l.frame = NSMakeRect(kOptionsPad, y, half, kOptionsRowH); r.frame = NSMakeRect(kOptionsPad + half + kGrid, y, half, kOptionsRowH); y += kOptionsRowH + kGrid; };
    pair(_hideDrumsButton, _fundamentalsButton);
    pair(_magnetButton, _grabOnClickButton);
    pair(_alignButton, _waveButton);
    pair(_syncButton, _samplerDragButton);
    _engineButton.frame = NSMakeRect(kOptionsPad, y, w, kOptionsRowH);
    y += kOptionsRowH + kGrid;
    _tuningMinus.frame = NSMakeRect(kOptionsPad, y, kOptionsRowH, kOptionsRowH);
    _tuningReadout.frame = NSMakeRect(kOptionsPad + kOptionsRowH + kGrid, y, w - 2 * (kOptionsRowH + kGrid), kOptionsRowH);
    _tuningPlus.frame = NSMakeRect(kOptionsPad + w - kOptionsRowH, y, kOptionsRowH, kOptionsRowH);
    y += kOptionsRowH + kGrid;
    _soundKnob.frame = NSMakeRect(kOptionsPad, y, third, kKnobH);
    _mixKnob.frame = NSMakeRect(kOptionsPad + third + kGrid, y, third, kKnobH);
    _soloButton.frame = NSMakeRect(kOptionsPad + 2 * (third + kGrid), y + (kKnobH - kOptionsRowH) / 2, third, kOptionsRowH);
    y += kKnobH + kGrid;
    _clearButton.frame = NSMakeRect(kOptionsPad, y, third, kOptionsRowH);
    _deleteButton.frame = NSMakeRect(kOptionsPad + third + kGrid, y, third, kOptionsRowH);
    _duplicateButton.frame = NSMakeRect(kOptionsPad + 2 * (third + kGrid), y, third, kOptionsRowH);
    y += kOptionsRowH + kGrid;
    _bridgeReadout.frame = NSMakeRect(kOptionsPad, y, w, kOptionsRowH);
    y += kOptionsRowH + kOptionsPad;
    _options.frame = NSMakeRect(W - kPad - kOptionsW, kTTRHeaderHeight + 4, kOptionsW, y);
}

- (void)layoutControls {
    const CGFloat W = self.bounds.size.width, H = self.bounds.size.height;
    const CGFloat leftW = W - kTTRPanelWidth;
    [self layoutHeader:W];

    // left: waveform lane + ruler, map, scroll bar, piano
    const CGFloat laneH = _showWave ? kTTRWaveLaneHeight : 0, waveH = kTTRRulerHeight + laneH;
    const CGFloat mapX = kTTRMargin + kTTRPianoWidth, mapW = MAX(60, leftW - mapX - kTTRMargin);
    const CGFloat waveY = kTTRHeaderHeight + kGrid, mapY = waveY + waveH + 4;
    const CGFloat scrollY = H - kGrid - kTTRScrollHeight;
    _wave.frame = NSMakeRect(mapX, waveY, mapW, waveH);
    _map.frame = NSMakeRect(mapX, mapY, mapW, MAX(60, scrollY - 4 - mapY));
    _piano.frame = NSMakeRect(kTTRMargin, mapY, kTTRPianoWidth, _map.frame.size.height);
    _scroll.frame = NSMakeRect(mapX, scrollY, mapW, kTTRScrollHeight);
    [self layoutSidebar:W height:H];
    [self layoutOptions:W];
    _banner.frame = NSMakeRect(mapX + kGrid, mapY + kGrid, MAX(60, mapW - 2 * kGrid), 40);
    _diag.frame = NSMakeRect(0, kTTRHeaderHeight, leftW, H - kTTRHeaderHeight);
    _about.frame = self.bounds;
    [self applyFitToView];
}

// ------------------------------------------------------------ drawing

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    const CGFloat leftW = b.size.width - kTTRPanelWidth;
    [TTR_BG setFill];
    NSRectFill(b);
    [TTR_PANEL setFill];
    NSRectFill(NSMakeRect(leftW, kTTRHeaderHeight, kTTRPanelWidth, b.size.height - kTTRHeaderHeight));
    NSRectFill(NSMakeRect(0, 0, b.size.width, kTTRHeaderHeight));
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, kTTRHeaderHeight - 1, b.size.width, 1));
    NSRectFill(NSMakeRect(leftW, kTTRHeaderHeight, 1, b.size.height - kTTRHeaderHeight));
    [self drawHeaderText];
}

// Centre of the header: the status (key, BPM, tuning, notes) and below it a dim one-line hint (hover help, messages, progress).
- (void)drawHeaderText {
    const NSRect c = _headerClip;
    if (c.size.width < 40) return;
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    style.alignment = NSTextAlignmentCenter;
    const BOOL isMessage = _transientMessage && CACurrentMediaTime() < _transientUntil;
    NSFont *statusFont = TTRFont(13, NSFontWeightSemibold), *hintFont = TTRFont(11);
    NSMutableDictionary* status = [TTRTextAttrs(statusFont, TTR_TEXT) mutableCopy];
    NSMutableDictionary* hint = [TTRTextAttrs(hintFont, isMessage ? TTR_TEXT : TTR_DIM) mutableCopy];
    status[NSParagraphStyleAttributeName] = style;
    hint[NSParagraphStyleAttributeName] = style;
    const CGFloat hintH = ceil(hintFont.ascender - hintFont.descender), statusH = ceil(statusFont.ascender - statusFont.descender);
    const BOOL hasHint = _statusText.length > 0;
    const CGFloat top = hasHint ? 7 : floor((kTTRHeaderHeight - statusH) / 2);
    [_headerText ?: @"" drawInRect:NSMakeRect(c.origin.x, top, c.size.width, statusH) withAttributes:status];
    if (hasHint) [_statusText drawInRect:NSMakeRect(c.origin.x, top + statusH + 2, c.size.width, hintH) withAttributes:hint];
}

@end
