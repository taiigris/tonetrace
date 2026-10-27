// Button actions, status line and the crosshair info of the main view.
#import "analysis/KeyEstimator.h"
#import "core/NoteEdit.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

static const double kSpeedSteps[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};

@implementation TTRMainView_v1 (Actions)

- (void)toggleRecord {
    const BOOL on = _kernel->param(ttr::kParamRecord) >= 0.5f;
    if (!on) { [self editParam:ttr::kParamPreview value:0.0f]; _fitZoom = YES; }  // never preview while capturing; the next recording is fitted again
    [self editParam:ttr::kParamRecord value:on ? 0.0f : 1.0f];
    [self refreshNow];
}

- (void)togglePreview {
    if (_kernel->status() != ttr::KernelStatus::Idle) return;
    [self editParam:ttr::kParamPreview value:_kernel->previewing() ? 0.0f : 1.0f];
    [self refreshNow];
}

// Next / previous entry of the speed ladder (1x = 100 px per second), counted from the current zoom (also from "Fit").
- (void)stepSpeed:(int)direction {
    const double current = _vp->pxPerSec / ttr::kBasePixelsPerSecond;
    double target = current;
    if (direction > 0) { for (double s : kSpeedSteps) if (s > current * 1.001) { target = s; break; } }
    else { for (int i = int(sizeof kSpeedSteps / sizeof kSpeedSteps[0]) - 1; i >= 0; --i) if (kSpeedSteps[i] < current * 0.999) { target = kSpeedSteps[i]; break; } }
    if (target == current) return;
    const double anchorSec = _vp->timeForX(_map.bounds.size.width / 2);  // zoom around the middle of the map
    _fitZoom = NO;
    [self editParam:ttr::kParamSpeed value:float(target)];
    _vp->pxPerSec = ttr::kBasePixelsPerSecond * target;
    _vp->scrollSec = anchorSec - _map.bounds.size.width / 2 / _vp->pxPerSec;
    [self refreshNow];
}

- (void)performReset {
    _kernel->resetAll();
    _fitZoom = YES;
    _vp->scrollSec = 0.0;
    [self showMessage:@"Recording and notes cleared - Undo brings them back"];
    [self refreshNow];
}

- (void)resetClicked {
    NSAlert* alert = [[NSAlert alloc] init];
    alert.messageText = @"Delete the recording and all notes?";
    alert.informativeText = @"Undo (Cmd+Z) brings them back as long as you have not closed the project.";
    [alert addButtonWithTitle:@"Delete"];
    [alert addButtonWithTitle:@"Cancel"];
    __weak TTRMainView_v1* weak = self;
    if (self.window) [alert beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse r) { if (r == NSAlertFirstButtonReturn) [weak performReset]; }];
    else if ([alert runModal] == NSAlertFirstButtonReturn) [self performReset];
}

// Quick Grab runs on a background thread; the result arrives as a notice. A second click cancels it.
- (void)grabClicked {
    if (_kernel->quickGrabRunning()) {
        _kernel->cancelQuickGrab();
        [self showMessage:@"Quick Grab cancelled"];
    } else if (!_kernel->startQuickGrab()) {
        [self showMessage:@"Quick Grab needs a finished recording"];
    }
    [self refreshNow];
}

// ------------------------------------------------------------ crosshair info + status

static NSString* formatPosition(double t) {
    const int minutes = int(t) / 60;
    return [NSString stringWithFormat:@"%d:%05.2f", minutes, t - minutes * 60.0];
}

static const float kTuningStepCents = 5.0f;

- (NSString*)tuningText {
    bool reliable = false;
    _kernel->detectedTuningCents(&reliable);
    const double offset = _kernel->tuningOffsetCents();
    if (!reliable && fabs(offset) < 0.5) return @"–";
    const long rounded = lround(offset);
    return rounded == 0 ? @"0 ct" : [NSString stringWithFormat:@"%@%ld ct", rounded < 0 ? @"\u2212" : @"+", labs(rounded)];
}

// Kernel and editor state (UiSettings) both follow the line view; the editor state survives closing the window.
- (void)lineViewChanged:(ttr::LineView)view {
    _kernel->setLineView(view);
    ttr::UiSettings ui = _bridge->uiSettings();
    ttr::storeLineView(_kernel->lineView(), &ui);
    _bridge->storeUiSettings(ui);
    [self refreshNow];
}

// The waveform lane is part of the editor state only (not a host parameter).
- (void)toggleWave {
    ttr::UiSettings ui = _bridge->uiSettings();
    ui.showWave = !ui.showWave;
    _bridge->storeUiSettings(ui);
    [self applyWaveVisibility:ui.showWave];
}

- (void)applyWaveVisibility:(BOOL)visible {
    _showWave = visible;
    _waveButton.isOn = visible;
    [self layoutControls];
    self.needsDisplay = YES;
    [self refreshNow];
}
- (void)nudgeTuning:(int)direction {
    ttr::LineView view = _kernel->lineView();
    view.trimCents = std::clamp(view.trimCents + float(direction) * kTuningStepCents, -ttr::kMaxTrimCents, ttr::kMaxTrimCents);
    [self lineViewChanged:view];
}

- (NSArray<NSString*>*)infoLines {
    if (!_vp->hasCursor) return @[ @"Note –", @"Move over the map", @"" ];
    const int pitch = std::clamp(int(std::lround(_vp->cursorPitch)), ttr::kMinNote, ttr::kMaxNote);
    const ttr::TimeGrid grid = _kernel->timeGrid();
    const double t = MAX(0.0, _vp->cursorSec), beat = grid.startPpq + t * grid.beatsPerSec();
    const long bar = long(floor(beat / 4.0)) + 1;
    const int beatInBar = int(floor(fmod(fmod(beat, 4.0) + 4.0, 4.0))) + 1;
    return @[ [NSString stringWithFormat:@"Note %@ · %.1f Hz", TTRNoteName(pitch), TTRPitchToHz(pitch)],
              [NSString stringWithFormat:@"Cursor %.1f Hz", TTRPitchToHz(_vp->cursorPitch)],
              [NSString stringWithFormat:@"%@ · Bar %ld.%d", formatPosition(t), bar, beatInBar] ];
}

- (NSString*)statusLineForSnapshot:(const ttr::SnapshotPtr&)snap status:(ttr::KernelStatus)status {
    if (_transientMessage && CACurrentMediaTime() < _transientUntil) return _transientMessage;
    if (_kernel->quickGrabRunning()) return [NSString stringWithFormat:@"Quick Grab %.0f %% – click Cancel to stop", _kernel->quickGrabProgress() * 100.0f];
    if (status == ttr::KernelStatus::Recording) return [NSString stringWithFormat:@"Recording %d:%02d – play your sample, press the red button to stop", int(_kernel->recordedSeconds()) / 60, int(_kernel->recordedSeconds()) % 60];
    if (status == ttr::KernelStatus::Analysing) return @"Analysing…";
    if (_hoverHelp.length > 0) return _hoverHelp;
    const int notes = snap ? int(snap->notes.size()) : 0;
    if (!snap || (!snap->hasRecording() && notes == 0)) return @"Press Record, then play your sample in your DAW";
    NSMutableString* s = [NSMutableString string];
    const int selected = snap ? ttr::selectedCount(snap->notes) : 0;
    if (selected > 0) [s appendFormat:@"%d selected", selected];
    if (_kernel->previewing()) [s appendString:s.length ? @" · Preview" : @"Preview"];
    if (snap && snap->hasRecording() && !snap->audio) [s appendString:s.length ? @" · notes only (recording too long to store)" : @"Notes only (recording too long to store)"];
    return s;
}

@end
