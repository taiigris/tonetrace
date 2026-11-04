#import "ui/TTRMainView.h"

#import "analysis/KeyEstimator.h"
#import "core/NoteEdit.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"
#import "ui/TTRViewportInput.h"

const NSSize kTTRDefaultSize = {1280, 720};
const NSSize kTTRMinimumSize = {1024, 600};

const CGFloat kTTRPanelWidth = 200, kTTRMargin = 8, kTTRPianoWidth = 40, kTTRHeaderHeight = 48, kTTRScrollHeight = 12, kTTRWaveLaneHeight = 32;
const double kTTRActiveHz = 30.0, kTTRIdleHz = 5.0;
static const CGFloat kMessageSeconds = 4.0, kFollowFraction = 0.85, kDefaultCenterPitch = 66.0;

@implementation TTRMainView_v1

- (instancetype)initWithBridge:(std::shared_ptr<ttr::TTRHostBridge>)bridge {
    self = [super initWithFrame:NSMakeRect(0, 0, kTTRDefaultSize.width, kTTRDefaultSize.height)];
    if (!self) return nil;
    _bridge = std::move(bridge);
    _kernel = _bridge->kernel();
    _vp = std::make_shared<TTRViewport>();
    _vp->pxPerSec = ttr::kBasePixelsPerSecond * _kernel->param(ttr::kParamSpeed);
    _tickHz = 0;
    _showWave = YES;
    _fitZoom = YES;
    _fitRows = YES;
    self.wantsLayer = YES;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self buildSubviews];
    [self registerSongDrop];
    [self layoutControls];
    _vp->scrollY = (ttr::kMaxNote + 0.5 - kDefaultCenterPitch) * _vp->rowH - _map.bounds.size.height / 2;  // start around the middle of the keyboard
    [self restoreUiSettings];
    [self applyFitToView];
    TTRClampViewport(_vp.get(), _map.bounds.size);
    [self refreshNow];
    return self;
}

- (void)dealloc {
    [_timer invalidate];
    [NSNotificationCenter.defaultCenter removeObserver:self];
}
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

// ------------------------------------------------------------ hover help and host-stolen keys

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
                                                       options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways | NSTrackingInVisibleRect
                                                         owner:self userInfo:nil]];
}

// The tooltip of the control under the pointer doubles as contextual help in the status bar.
- (void)setHoverHelp:(NSString*)help {
    if ((help == nil && _hoverHelp == nil) || [help isEqualToString:_hoverHelp]) return;
    _hoverHelp = [help copy];
    [self refreshNow];
}

- (void)mouseMoved:(NSEvent*)event {
    NSView* hit = [self hitTest:[self.superview convertPoint:event.locationInWindow fromView:nil]];
    while (hit && hit != self && hit.toolTip.length == 0) hit = hit.superview;
    [self setHoverHelp:hit && hit != self ? hit.toolTip : nil];
}

- (void)mouseExited:(NSEvent*)event { [self setHoverHelp:nil]; }

// Live and some other hosts take Cmd+Z, Backspace and Space for themselves; whatever still arrives is handled here.
- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (!(event.modifierFlags & NSEventModifierFlagCommand)) return [super performKeyEquivalent:event];
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@"z"]) { (event.modifierFlags & NSEventModifierFlagShift) ? [self redoClicked] : [self undoClicked]; return YES; }
    if ([key isEqualToString:@"a"]) { [_map selectAllNotes]; return YES; }
    if ([key isEqualToString:@"d"]) { [self duplicateClicked]; return YES; }
    return [super performKeyEquivalent:event];
}

- (void)resizeSubviewsWithOldSize:(NSSize)oldSize { [self layoutControls]; }
- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self layoutControls];
    self.needsDisplay = YES;
}

// ------------------------------------------------------------ timer

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [NSNotificationCenter.defaultCenter removeObserver:self];
    if (!self.window) { [self shutdown]; return; }
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(windowVisibilityChanged:) name:NSWindowDidChangeOcclusionStateNotification object:self.window];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(windowVisibilityChanged:) name:NSWindowDidMiniaturizeNotification object:self.window];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(windowVisibilityChanged:) name:NSWindowDidDeminiaturizeNotification object:self.window];
    [self windowVisibilityChanged:nil];
}

- (void)windowVisibilityChanged:(NSNotification*)note {
    NSWindow* w = self.window;
    _isWindowVisible = w && !w.isMiniaturized && (w.occlusionState & NSWindowOcclusionStateVisible);
    if (_isWindowVisible) { [self updateTimer]; [self refreshNow]; }
    else { [_timer invalidate]; _timer = nil; _tickHz = 0; }
}

- (void)shutdown {
    [self closeOptions];
    [_timer invalidate];
    _timer = nil;
    _tickHz = 0;
    _isWindowVisible = NO;
}

// ------------------------------------------------------------ refresh

- (void)showMessage:(NSString*)message {
    _transientMessage = [message copy];
    _transientUntil = CACurrentMediaTime() + kMessageSeconds;
    [self setNeedsDisplayInRect:NSMakeRect(0, 0, self.bounds.size.width, kTTRHeaderHeight)];
}

static NSString* formatBpm(double bpm) {
    return fabs(bpm - round(bpm)) < 0.05 ? [NSString stringWithFormat:@"%.0f", bpm] : [NSString stringWithFormat:@"%.1f", bpm];
}

static NSString* formatSpeed(double speed) {
    return fabs(speed - round(speed)) < 0.005 ? [NSString stringWithFormat:@"%.0fx", speed] : [NSString stringWithFormat:@"%.2gx", speed];
}

// Called when the map, lane, piano or scroll bar changed the shared window.
- (void)viewportChanged {
    if (_fitRows && fabs(_vp->rowH - _fitRowH) > 0.01) _fitRows = NO;  // the user zoomed the keyboard by hand
    _scroll.offset = _vp->scrollSec;
    [_wave setNeedsDisplay:YES];
    [_map setNeedsDisplay:YES];
    [_piano setNeedsDisplay:YES];
    _info.lines = [self infoLines];
}

- (void)updateViewportForStatus:(ttr::KernelStatus)status recorded:(double)recorded mapView:(const ttr::MapView&)mv {
    _vp->pxPerSec = ttr::kBasePixelsPerSecond * _kernel->param(ttr::kParamSpeed);
    double content = MAX(mv.lengthSec, recorded);
    if (mv.snapshot) for (const ttr::NoteEvent& n : mv.snapshot->notes) content = MAX(content, n.endSec);
    _vp->contentSec = MAX(content, 1.0);
    const BOOL fits = _fitZoom && mv.snapshot && mv.snapshot->hasRecording() && status != ttr::KernelStatus::Recording;
    if (fits) {  // the whole recording fills the map width
        _vp->pxPerSec = _map.bounds.size.width / _vp->contentSec;
        _vp->scrollSec = 0.0;
    }
    const double visibleSec = _map.bounds.size.width / _vp->pxPerSec;
    if (status == ttr::KernelStatus::Recording && recorded > _vp->scrollSec + kFollowFraction * visibleSec)
        _vp->scrollSec = recorded - kFollowFraction * visibleSec;  // the view follows the recording
    TTRClampViewport(_vp.get(), _map.bounds.size);
    _scroll.contentLength = fits ? _vp->contentSec : _vp->contentSec + 2.0;
    _scroll.visibleLength = fits ? _vp->contentSec : visibleSec;
    _scroll.offset = _vp->scrollSec;
}

// Key · BPM · Tuning · notes, the status in the middle of the header.
- (NSString*)headerTextForSnapshot:(const ttr::SnapshotPtr&)snap recording:(BOOL)recording {
    NSMutableArray<NSString*>* parts = [NSMutableArray array];
    [parts addObject:_keyReadout.value ?: @"-"];
    [parts addObject:[NSString stringWithFormat:@"%@ BPM", _bpmReadout.value ?: @"-"]];
    NSString* tuning = [self tuningText];
    if (![tuning isEqualToString:@"–"]) [parts addObject:[NSString stringWithFormat:@"Tuning %@", tuning]];
    const int count = snap ? int(snap->notes.size()) : 0;
    [parts addObject:count == 1 ? @"1 note" : [NSString stringWithFormat:@"%d notes", count]];
    (void)recording;
    return [parts componentsJoinedByString:@"   ·   "];
}

- (void)refreshNow {
    const ttr::MapView mv = _kernel->mapView();
    const ttr::KernelStatus status = _kernel->status();
    const double recorded = _kernel->recordedSeconds(), playhead = _kernel->playheadSec();
    const ttr::SnapshotPtr snap = mv.snapshot;
    const BOOL recording = status == ttr::KernelStatus::Recording, busy = status != ttr::KernelStatus::Idle;
    const int noteCount = snap ? int(snap->notes.size()) : 0;
    const BOOL hasRecording = snap && snap->hasRecording();

    [self updateViewportForStatus:status recorded:recorded mapView:mv];
    [_map updateWithMapView:mv status:status recorded:recorded playhead:playhead];
    [_wave updateWithMapView:mv status:status recorded:recorded playhead:playhead];
    [_piano setNeedsDisplay:YES];

    _recordButton.isOn = _kernel->param(ttr::kParamRecord) >= 0.5f;
    _recordButton.isEnabled = status != ttr::KernelStatus::Analysing;
    _previewButton.isOn = _kernel->previewing();
    _previewButton.isEnabled = !busy && (hasRecording || noteCount > 0);
    _dragButton.isEnabled = !busy && noteCount > 0;
    _saveButton.isEnabled = !busy && noteCount > 0;
    _clearButton.isEnabled = noteCount > 0 && !busy;
    _resetButton.isEnabled = snap != nullptr && !busy;
    const BOOL grabRunning = _kernel->quickGrabRunning();
    _grabButton.title = grabRunning ? @"Cancel" : @"Grab";
    _grabButton.isEnabled = grabRunning || (!busy && hasRecording);
    _loadSongButton.isEnabled = !busy && !grabRunning;
    _syncButton.isOn = _kernel->syncToHost();
    [self updateEditStates];
    _soloButton.isOn = _kernel->param(ttr::kParamSolo) >= 0.5f;
    _hideDrumsButton.isOn = _kernel->param(ttr::kParamHideDrums) >= 0.5f;
    const ttr::LineView lineView = _kernel->lineView();
    _fundamentalsButton.isOn = lineView.fundamentals;
    _viewButton.isOn = !lineView.isSpectrum;  // the icon shows the view a click would NOT leave: lines when on
    _grabOnClickButton.isOn = lineView.grabOnClick;
    _samplerDragButton.isOn = lineView.samplerOnDrag;
    _engineButton.isOn = lineView.engine != ttr::GrabEngine::Classic;
    NSString* engineTitle = [NSString stringWithFormat:@"Engine: %s", ttr::grabEngineName(lineView.engine)];
    if (![_engineButton.title isEqualToString:engineTitle]) _engineButton.title = engineTitle;
    _samplerButton.isEnabled = !busy && noteCount > 0 && !_samplerPending;
    [self updateBridgeStatus];
    _alignButton.isOn = lineView.alignToSample;
    _tuningReadout.value = [self tuningText];
    _tuningReadout.isHighlighted = lineView.alignToSample && _kernel->detectedTuningCents() != 0.0;
    _tuningMinus.isEnabled = lineView.trimCents > -ttr::kMaxTrimCents;
    _tuningPlus.isEnabled = lineView.trimCents < ttr::kMaxTrimCents;
    _magnetButton.isOn = _kernel->param(ttr::kParamMagnet) >= 0.5f;
    for (const auto& pair : std::vector<std::pair<TTRKnob_v1*, ttr::ParamId>>{{_gainKnob, ttr::kParamGain}, {_rangeKnob, ttr::kParamRange}, {_soundKnob, ttr::kParamSoundVolume}, {_mixKnob, ttr::kParamMix}})
        if (!pair.first.isDragging) pair.first.value = _kernel->param(pair.second);
    if (!_quickGrabSlider.isDragging) _quickGrabSlider.value = _kernel->param(ttr::kParamQuickGrab);
    _snap.selectedIndex = NSInteger(_kernel->param(ttr::kParamSnap));
    _speedReadout.value = _fitZoom && hasRecording && !recording ? @"Fit" : formatSpeed(_vp->pxPerSec / ttr::kBasePixelsPerSecond);

    const ttr::KeyResult key = snap ? snap->key : ttr::KeyResult{};
    _keyReadout.value = recording ? @"-" : [NSString stringWithUTF8String:ttr::keyName(key).c_str()];
    _keyReadout.isHighlighted = key.root >= 0 && !recording;
    _bpmReadout.value = formatBpm(snap && snap->hasRecording() ? snap->bpm : _kernel->hostBpm());
    _info.lines = [self infoLines];

    NSString* text = [self statusLineForSnapshot:snap status:status];
    NSString* header = [self headerTextForSnapshot:snap recording:recording];
    if (![text isEqualToString:_statusText] || ![header isEqualToString:_headerText]) {
        _statusText = text;
        _headerText = header;
        [self setNeedsDisplayInRect:NSMakeRect(0, 0, self.bounds.size.width, kTTRHeaderHeight)];
    }
    [self pollNotices];
    [self updateDiagnostics];
    [self storeUiSettingsIfChanged];
    [self updateTimer];
}

@end
