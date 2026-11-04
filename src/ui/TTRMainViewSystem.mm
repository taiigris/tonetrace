// Everything that is not a control: notices as banners, the refresh timer, the diagnostics panel and the About card,
// and the editor state (zoom, scroll, follow) that the host stores with the project.
#include <algorithm>
#include <cmath>

#include "analysis/ml/MuScriptorEngine.h"
#import "ui/TTRMainViewInternal.h"

@implementation TTRMainView_v1 (System)

- (void)showBanner:(NSString*)text level:(ttr::NoticeLevel)level {
    [_banner showText:text level:level];
}

- (void)pollNotices {
    ttr::Notice n;
    while (_kernel->diagnostics().popNotice(&n)) [self showBanner:[NSString stringWithUTF8String:n.text.c_str()] level:n.level];
}

// 30 Hz while something moves (playback, transcription, recording, a meter falling back), 5 Hz otherwise.
- (void)updateTimer {
    if (!_isWindowVisible) {
        [_timer invalidate];
        _timer = nil;
        return;
    }
    const BOOL active = _kernel->isPlaying() || _kernel->isWorking() || _kernel->isRecording() || _kernel->masterLevel() > 1e-4f ||
                        (_doc && _doc->transcription.isActive());
    const double hz = active ? kTTRActiveHz : kTTRIdleHz;
    if (_timer && hz == _tickHz) return;
    [_timer invalidate];
    _tickHz = hz;
    __weak TTRMainView_v1* weak = self;
    _timer = [NSTimer timerWithTimeInterval:1.0 / hz repeats:YES block:^(NSTimer*) { [weak refreshNow]; }];
    [[NSRunLoop currentRunLoop] addTimer:_timer forMode:NSRunLoopCommonModes];
}

- (void)toggleDiagnostics {
    if (_about.isShowing) [_about dismiss];
    _diag.hidden = !_diag.hidden;
    [self updateDiagnostics];
}

- (void)showAbout { [_about show]; }

- (void)updateDiagnostics {
    const CFTimeInterval now = CACurrentMediaTime();
    if (_lastFpsTime == 0) _lastFpsTime = now;
    if (now - _lastFpsTime >= 1.0) {
        _fps = double(_lastDrawCount) / (now - _lastFpsTime);
        _lastDrawCount = 0;
        _lastFpsTime = now;
    }
    if (_diag.hidden) return;
    ttr::DiagnosticsSnapshot d = _kernel->diagnosticsSnapshot();
    d.muscriptorEngine = ttr::transcriberStatus();
    d.uiFps = _fps;
    [_diag updateWithSnapshot:d];
}

- (void)restoreUiSettings {
    const ttr::UiSettings s = _bridge->uiSettings();
    _tl->isFit = s.pixelsPerSecond <= 0.0;
    if (!_tl->isFit) _tl->pxPerSec = s.pixelsPerSecond;
    _tl->rowH = s.rowHeight;
    _tl->scrollSec = s.scrollSec;
    _tl->follow = s.followPlayhead;
    if (s.scrollY >= 0.0) _tl->scrollY = s.scrollY;
    _storedUi = s;
    _diag.hidden = YES;
}

- (void)storeUiSettingsIfChanged {
    ttr::UiSettings s = _storedUi;
    s.pixelsPerSecond = _tl->isFit ? 0.0 : _tl->pxPerSec;
    s.rowHeight = _tl->rowH;
    s.scrollSec = _tl->scrollSec;
    s.scrollY = _tl->scrollY;
    s.followPlayhead = _tl->follow;
    s.width = int(std::lround(self.bounds.size.width));
    s.height = int(std::lround(self.bounds.size.height));
    s = ttr::sanitized(s);
    if (s == _storedUi) return;
    _storedUi = s;
    _bridge->storeUiSettings(s);
}

@end
