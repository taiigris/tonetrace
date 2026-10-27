// Main view: timer rate, hover help, notice banners, diagnostics panel, editor state and the status bar buttons.
#import "core/NoteEdit.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

static const CGFloat kStatusButtonH = 20, kStatusButtonGap = 4, kBannerHeight = 40, kBannerInset = 8;
static const double kViewportEpsilon = 0.5, kFpsWindowSeconds = 1.0;

@implementation TTRMainView_v1 (System)

// ------------------------------------------------------------ timer

- (void)updateTimer {
    if (!_isWindowVisible || !self.window) return;
    const BOOL isActive = _kernel->status() != ttr::KernelStatus::Idle || _kernel->previewing() || _kernel->quickGrabRunning() || !_diag.hidden;
    const double hz = isActive ? kTTRActiveHz : kTTRIdleHz;
    if (_timer && hz == _tickHz) return;
    [_timer invalidate];
    __weak TTRMainView_v1* weak = self;
    _timer = [NSTimer timerWithTimeInterval:1.0 / hz repeats:YES block:^(NSTimer*) { [weak refreshNow]; }];
    [[NSRunLoop mainRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
    _tickHz = hz;
}

// ------------------------------------------------------------ edit buttons

- (void)undoClicked { [_map undoEdit]; [self refreshNow]; }
- (void)redoClicked { [_map redoEdit]; [self refreshNow]; }
- (void)deleteClicked { [_map deleteSelectedNotes]; [self refreshNow]; }
- (void)duplicateClicked { [_map duplicateSelectedNotes]; [self refreshNow]; }
- (void)syncClicked {
    _bridge->setSyncToHost(!_kernel->syncToHost());
    [self showMessage:_kernel->syncToHost() ? @"Sync to host on: the preview follows the host's song position" : @"Sync to host off"];
    [self refreshNow];
}

- (void)updateEditStates {
    const ttr::SnapshotPtr snap = _kernel->snapshot();
    const BOOL hasSelection = snap && ttr::selectedCount(snap->notes) > 0;
    const BOOL busy = _kernel->status() != ttr::KernelStatus::Idle;
    _undoButton.isEnabled = !busy && _kernel->canUndo();
    _redoButton.isEnabled = !busy && _kernel->canRedo();
    _deleteButton.isEnabled = hasSelection;
    _duplicateButton.isEnabled = hasSelection;
}

// ------------------------------------------------------------ banners and diagnostics

- (void)showBanner:(NSString*)text level:(ttr::NoticeLevel)level {
    [_banner showText:text level:level];
}

- (void)pollNotices {
    ttr::Notice notice;
    while (_kernel->diagnostics().popNotice(&notice)) [self showBanner:[NSString stringWithUTF8String:notice.text.c_str()] level:notice.level];
}

- (void)toggleDiagnostics {
    _diag.hidden = !_diag.hidden;
    [self refreshNow];
}

- (void)openDiagnostics {
    if (_diag.hidden) [self toggleDiagnostics];
}

- (void)openLogFolder {
    [_diag openLogFolder];
}

- (void)showAbout {
    [_about show];
}

- (void)updateDiagnostics {
    const CFTimeInterval now = CACurrentMediaTime();
    if (now - _lastFpsTime >= kFpsWindowSeconds) {
        _fps = double(_vp->drawCount - _lastDrawCount) / (now - _lastFpsTime);
        _lastDrawCount = _vp->drawCount;
        _lastFpsTime = now;
    }
    if (_diag.hidden) return;
    ttr::DiagnosticsSnapshot d = _kernel->diagnosticsSnapshot();
    d.uiFps = _fps;
    d.uiFrameMs = _vp->frameMsAvg;
    [_diag updateWithSnapshot:d];
}

// ------------------------------------------------------------ editor state (zoom, scroll, view settings)

- (void)restoreUiSettings {
    const ttr::UiSettings ui = _bridge->uiSettings();
    _vp->scrollSec = ui.scrollSec;
    if (ui.scrollY >= 0.0) _vp->scrollY = ui.scrollY;
    _kernel->setLineView(ttr::lineViewOf(ui));
    _showWave = ui.showWave;
    _waveButton.isOn = ui.showWave;
    [self layoutControls];
    _storedScrollSec = ui.scrollSec;
    _storedScrollY = _vp->scrollY;
    _storedSize = self.bounds.size;
}

- (void)storeUiSettingsIfChanged {
    const NSSize size = self.bounds.size;
    if (fabs(_vp->scrollSec - _storedScrollSec) < kViewportEpsilon / MAX(1.0, _vp->pxPerSec) && fabs(_vp->scrollY - _storedScrollY) < kViewportEpsilon &&
        size.width == _storedSize.width && size.height == _storedSize.height)
        return;
    _storedScrollSec = _vp->scrollSec;
    _storedScrollY = _vp->scrollY;
    _storedSize = size;
    ttr::UiSettings ui = _bridge->uiSettings();
    ui.scrollSec = _vp->scrollSec;
    ui.scrollY = _vp->scrollY;
    ui.width = int(size.width);
    ui.height = int(size.height);
    _bridge->storeUiSettings(ui);
}

// ------------------------------------------------------------ status bar

- (void)layoutStatusButtons {
    const CGFloat leftW = self.bounds.size.width - kTTRPanelWidth, y = self.bounds.size.height - kTTRStatusHeight + 3;
    const CGFloat widths[] = {46, 46, 54, 70};
    TTRButton_v1* buttons[] = {_undoButton, _redoButton, _deleteButton, _duplicateButton};
    CGFloat x = leftW - kTTRMargin;
    for (int i = 3; i >= 0; --i) {
        x -= widths[i];
        buttons[i].frame = NSMakeRect(x, y, widths[i], kStatusButtonH);
        x -= kStatusButtonGap;
    }
    const NSRect map = _map.frame;
    _banner.frame = NSMakeRect(map.origin.x + kBannerInset, map.origin.y + kBannerInset, MAX(60, map.size.width - 2 * kBannerInset), kBannerHeight);
    _diag.frame = NSMakeRect(0, 0, leftW, self.bounds.size.height - kTTRStatusHeight);
    _about.frame = self.bounds;
}

- (void)drawStatusBar:(NSRect)status {
    const CGFloat buttonsW = 46 + 46 + 54 + 70 + 3 * kStatusButtonGap + 2 * kTTRMargin;
    NSColor* color = _transientMessage && CACurrentMediaTime() < _transientUntil ? TTR_SELECT : _kernel->status() != ttr::KernelStatus::Idle ? TTR_TEXT : TTR_DIM;
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary* attrs = [TTRTextAttrs(TTRFont(12, NSFontWeightMedium), color) mutableCopy];
    attrs[NSParagraphStyleAttributeName] = style;
    const CGFloat textH = ceil(TTRFont(12, NSFontWeightMedium).ascender - TTRFont(12, NSFontWeightMedium).descender);
    [_statusText ?: @"" drawInRect:NSMakeRect(kTTRMargin, status.origin.y + floor((status.size.height - textH) / 2), MAX(40, status.size.width - buttonsW - kTTRMargin), textH)
                    withAttributes:attrs];
}

@end
