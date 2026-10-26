#import "ui/TTRMainView.h"

#import "analysis/KeyEstimator.h"
#import "core/NoteEdit.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"
#import "ui/TTRViewportInput.h"

const NSSize kTTRDefaultSize = {1100, 640};
const NSSize kTTRMinimumSize = {820, 520};

const CGFloat kTTRPanelWidth = 190, kTTRMargin = 10, kTTRPianoWidth = 54, kTTRStatusHeight = 26, kTTRScrollHeight = 12;
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
    self.wantsLayer = YES;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self buildSubviews];
    [self layoutControls];
    _vp->scrollY = (ttr::kMaxNote + 0.5 - kDefaultCenterPitch) * _vp->rowH - _map.bounds.size.height / 2;  // start around the middle of the keyboard
    [self restoreUiSettings];
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

// ------------------------------------------------------------ construction

- (TTRButton_v1*)addButton:(NSString*)title kind:(TTRButtonKind_v1)kind tip:(NSString*)tip action:(void (^)(void))action {
    TTRButton_v1* b = [[TTRButton_v1 alloc] initWithTitle:title kind:kind];
    b.toolTip = tip;
    b.onClick = action;
    [self addSubview:b];
    return b;
}

- (TTRButton_v1*)addToggle:(NSString*)title param:(ttr::ParamId)paramId tip:(NSString*)tip {
    __weak TTRMainView_v1* weak = self;
    auto kernel = _kernel;
    return [self addButton:title kind:TTRButtonKindNormal tip:tip action:^{
        [weak editParam:paramId value:kernel->param(paramId) >= 0.5f ? 0.0f : 1.0f];
        [weak refreshNow];
    }];
}

- (TTRKnob_v1*)addKnob:(NSString*)title param:(ttr::ParamId)paramId tip:(NSString*)tip format:(NSString * (^)(double))format {
    TTRKnob_v1* knob = [[TTRKnob_v1 alloc] initWithFrame:NSMakeRect(0, 0, 82, 58)];
    knob.title = title;
    knob.isCompact = YES;
    knob.defaultValue = ttr::paramSpec(paramId).defaultValue;
    knob.toolTip = tip;
    knob.valueFormatter = format;
    auto bridge = _bridge;
    knob.onBeginEdit = ^{ bridge->beginEdit(paramId); };
    knob.onChange = ^(double v) { bridge->setParameter(paramId, float(v)); };
    knob.onEndEdit = ^{ bridge->endEdit(paramId); };
    [self addSubview:knob];
    return knob;
}

- (void)editParam:(int)paramId value:(float)value {
    _bridge->beginEdit(paramId);
    _bridge->setParameter(paramId, value);
    _bridge->endEdit(paramId);
}

- (void)buildSubviews {
    __weak TTRMainView_v1* weak = self;
    auto kernel = _kernel;
    NSString * (^percent)(double) = ^NSString*(double v) { return [NSString stringWithFormat:@"%.0f%%", v * 100]; };

    // waveform lane, note map, piano keys, scroll bar
    _wave = [[TTRWaveformView_v1 alloc] initWithKernel:_kernel viewport:_vp];
    _map = [[TTRNoteMapView_v1 alloc] initWithKernel:_kernel viewport:_vp];
    _piano = [[TTRPianoKeys_v1 alloc] initWithViewport:_vp];
    _scroll = [[TTRScrollBar_v1 alloc] initWithFrame:NSZeroRect];
    void (^zoom)(double) = ^(double speed) { [weak editParam:ttr::kParamSpeed value:float(speed)]; };
    _wave.toolTip = @"Waveform: click sets the preview start, drag selects a loop region, wheel scrolls and zooms";
    _map.toolTip = @"Note map: click a line to grab it as a note, drag on empty space to draw one, drag a note to move it, drag its ends to resize, right-click for the menu";
    _piano.toolTip = @"Piano: click a key to hear the pitch";
    _scroll.toolTip = @"Scroll the timeline";
    _wave.onZoom = zoom;
    _map.onZoom = zoom;
    _map.onMessage = ^(NSString* m) { [weak showMessage:m]; };
    _map.onTogglePreview = ^{ [weak togglePreview]; };
    _wave.onViewportChange = ^{ [weak viewportChanged]; };
    _map.onViewportChange = ^{ [weak viewportChanged]; };
    _piano.onScroll = ^{ [weak viewportChanged]; };
    _piano.onKeyClick = ^(int pitch) { kernel->audition(pitch); };
    _scroll.onScroll = ^(double offset) {
        TTRMainView_v1* self = weak;
        if (!self) return;
        self->_vp->scrollSec = offset;
        [self viewportChanged];
    };
    for (NSView* v in @[ _wave, _map, _piano, _scroll ]) [self addSubview:v];

    // side panel
    _gainKnob = [self addKnob:@"Gain" param:ttr::kParamGain tip:@"Brightness of the map" format:^NSString*(double v) { return [NSString stringWithFormat:@"%+.0f dB", (v - 0.5) * 40]; }];
    _rangeKnob = [self addKnob:@"Range" param:ttr::kParamRange tip:@"Dynamic range of the map: lower = only the strongest lines" format:^NSString*(double v) { return [NSString stringWithFormat:@"%.0f dB", 18 + v * 50]; }];
    _speedMinus = [self addButton:@"−" kind:TTRButtonKindNormal tip:@"Zoom out in time" action:^{ [weak stepSpeed:-1]; }];
    _speedPlus = [self addButton:@"+" kind:TTRButtonKindNormal tip:@"Zoom in in time" action:^{ [weak stepSpeed:1]; }];
    _speedReadout = [[TTRReadout_v1 alloc] initWithFrame:NSZeroRect];
    _speedReadout.title = @"Speed";
    _speedReadout.toolTip = @"Time zoom (Cmd+wheel or pinch)";
    [self addSubview:_speedReadout];
    _hideDrumsButton = [self addToggle:@"Hide drums" param:ttr::kParamHideDrums tip:@"Show only the harmonic part of the sound: drum hits fade out of the map (and out of Quick Grab)"];
    _magnetButton = [self addToggle:@"Magnet" param:ttr::kParamMagnet tip:@"New notes snap to the strongest line within one semitone of the pointer"];
    _fundamentalsButton = [self addButton:@"Fundamentals" kind:TTRButtonKindNormal tip:@"Fundamentals: overtones (2x to 6x and beyond of a stronger, lower line) are drawn dark, so every note shows only its fundamental line; Quick Grab prefers fundamentals"
                                      action:^{ ttr::LineView v = kernel->lineView(); v.fundamentals = !v.fundamentals; [weak lineViewChanged:v]; }];
    _alignButton = [self addButton:@"Align" kind:TTRButtonKindNormal tip:@"Align to sample: shifts the semitone rows by the sample's detected tuning, so the lines sit in the middle of their rows (notes get the right MIDI pitch)"
                               action:^{ ttr::LineView v = kernel->lineView(); v.alignToSample = !v.alignToSample; [weak lineViewChanged:v]; }];
    _tuningMinus = [self addButton:@"−" kind:TTRButtonKindNormal tip:@"Tuning trim -5 cent (move the rows down)" action:^{ [weak nudgeTuning:-1]; }];
    _tuningPlus = [self addButton:@"+" kind:TTRButtonKindNormal tip:@"Tuning trim +5 cent (move the rows up), at most 50 cent" action:^{ [weak nudgeTuning:1]; }];
    _tuningReadout = [[TTRReadout_v1 alloc] initWithFrame:NSZeroRect];
    _tuningReadout.title = @"Tuning";
    _tuningReadout.toolTip = @"Detected deviation of the sample from A = 440 Hz (plus your trim). The rows follow it while Align is on";
    [self addSubview:_tuningReadout];
    _snap = [[TTRDropdown_v1 alloc] initWithFrame:NSZeroRect];
    _snap.title = @"Snap";
    NSMutableArray* names = [NSMutableArray array];
    for (int i = 0; i < ttr::kNumSnapModes; ++i) [names addObject:[NSString stringWithUTF8String:ttr::snapName(i)]];
    _snap.items = names;
    _snap.toolTip = @"Snap: Onset (default) snaps note starts and ends to detected onsets and line ends; 1/4 to 1/32 snap to the grid (also for the exported MIDI); Off is free. Hold Alt to draw without snap";
    _snap.onChange = ^(NSInteger i) { [weak editParam:ttr::kParamSnap value:float(i)]; };
    [self addSubview:_snap];
    _quickGrabKnob = [self addKnob:@"Quick Grab" param:ttr::kParamQuickGrab tip:@"How far below the strongest line Quick Grab still picks up notes" format:percent];
    _grabButton = [self addButton:@"Grab" kind:TTRButtonKindNormal tip:@"Create notes from the strongest lines as a starting point" action:^{ [weak grabClicked]; }];

    _recordButton = [[TTRRecordButton_v1 alloc] initWithFrame:NSZeroRect];
    _recordButton.toolTip = @"Record the audio passing through this plugin";
    _recordButton.onClick = ^{ [weak toggleRecord]; };
    [self addSubview:_recordButton];
    _resetButton = [self addButton:@"Reset" kind:TTRButtonKindNormal tip:@"Delete the recording and all notes (Cmd-click skips the question)" action:^{ [weak resetClicked]; }];
    _clearButton = [self addButton:@"Clear notes" kind:TTRButtonKindNormal tip:@"Remove every note (can be undone)" action:^{ kernel->editNotes({}); }];
    _previewButton = [self addButton:@"Preview" kind:TTRButtonKindPlay tip:@"Play the recording together with your notes (Space)" action:^{ [weak togglePreview]; }];
    _soloButton = [self addToggle:@"Solo" param:ttr::kParamSolo tip:@"Mute the incoming signal while the preview plays"];
    _soundKnob = [self addKnob:@"Sound" param:ttr::kParamSoundVolume tip:@"Volume of the note synth in the preview" format:percent];
    _mixKnob = [self addKnob:@"Mix" param:ttr::kParamMix tip:@"Preview balance: recording (left) and notes (right)" format:^NSString*(double v) { return v < 0.02 ? @"Sample" : v > 0.98 ? @"Notes" : [NSString stringWithFormat:@"%.0f%%", v * 100]; }];

    _dragButton = [[TTRDragButton_v1 alloc] initWithTitle:@"Drag MIDI" kind:TTRButtonKindDrag];
    _dragButton.toolTip = @"Drag the selected notes (or all notes) into a MIDI track or clip";
    _dragButton.dragFileProvider = ^NSURL*(void) { return [weak writeNotesToMidiFile]; };
    _dragButton.onDragRejected = ^{ [weak showMessage:@"Draw at least one note, then drag"]; };
    [self addSubview:_dragButton];
    _saveButton = [self addButton:@"Save .mid" kind:TTRButtonKindNormal tip:@"Save the selected notes (or all notes) as a MIDI file" action:^{ [weak saveNotesAsMidi]; }];
    _syncButton = [self addButton:@"Sync" kind:TTRButtonKindNormal tip:@"Sync to host: while the host transport runs, the preview follows the song position (relative to where you recorded)" action:^{ [weak syncClicked]; }];
    _infoButton = [self addButton:@"ⓘ" kind:TTRButtonKindNormal tip:@"Diagnostics: version, host, load, log folder" action:^{ [weak toggleDiagnostics]; }];
    _undoButton = [self addButton:@"Undo" kind:TTRButtonKindNormal tip:@"Undo the last edit, Quick Grab, Clear or Reset (Cmd+Z)" action:^{ [weak undoClicked]; }];
    _redoButton = [self addButton:@"Redo" kind:TTRButtonKindNormal tip:@"Redo (Cmd+Shift+Z)" action:^{ [weak redoClicked]; }];
    _deleteButton = [self addButton:@"Delete" kind:TTRButtonKindNormal tip:@"Delete the selected notes (Backspace, right-click menu)" action:^{ [weak deleteClicked]; }];
    _duplicateButton = [self addButton:@"Duplicate" kind:TTRButtonKindNormal tip:@"Duplicate the selected notes behind the selection (Cmd+D)" action:^{ [weak duplicateClicked]; }];
    _banner = [[TTRBannerView_v1 alloc] initWithFrame:NSZeroRect];
    _diag = [[TTRDiagnosticsView_v1 alloc] initWithFrame:NSZeroRect];
    _diag.onClose = ^{ [weak toggleDiagnostics]; };
    _diag.onMessage = ^(NSString* m) { [weak showMessage:m]; };
    _diag.toolTip = @"Diagnostics";
    [self addSubview:_banner];
    [self addSubview:_diag];
    _logoView = [[TTRLogoView_v1 alloc] initWithFrame:NSZeroRect];
    _logoView.toolTip = @"About Tonetrace";
    _logoView.onClick = ^{ [weak showAbout]; };
    [self addSubview:_logoView];
    _about = [[TTRAboutView_v1 alloc] initWithFrame:NSZeroRect];  // topmost: dims the whole editor
    _about.onOpenDiagnostics = ^{ [weak openDiagnostics]; };
    _about.onOpenLogFolder = ^{ [weak openLogFolder]; };
    _keyReadout = [[TTRReadout_v1 alloc] initWithFrame:NSZeroRect];
    _keyReadout.title = @"Key";
    _keyReadout.isStacked = YES;
    _bpmReadout = [[TTRReadout_v1 alloc] initWithFrame:NSZeroRect];
    _bpmReadout.title = @"BPM";
    _bpmReadout.isStacked = YES;
    _info = [[TTRInfoBox_v1 alloc] initWithFrame:NSZeroRect];
    for (NSView* v in @[ _keyReadout, _bpmReadout, _info ]) [self addSubview:v];
    [self addSubview:_about];  // keep last: the overlay has to cover every other view
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
    [_timer invalidate];
    _timer = nil;
    _tickHz = 0;
    _isWindowVisible = NO;
}

// ------------------------------------------------------------ refresh

- (void)showMessage:(NSString*)message {
    _transientMessage = [message copy];
    _transientUntil = CACurrentMediaTime() + kMessageSeconds;
    [self setNeedsDisplayInRect:NSMakeRect(0, self.bounds.size.height - kTTRStatusHeight, self.bounds.size.width - kTTRPanelWidth, kTTRStatusHeight)];
}

static NSString* formatBpm(double bpm) {
    return fabs(bpm - round(bpm)) < 0.05 ? [NSString stringWithFormat:@"%.0f", bpm] : [NSString stringWithFormat:@"%.1f", bpm];
}

static NSString* formatSpeed(double speed) {
    return fabs(speed - round(speed)) < 0.005 ? [NSString stringWithFormat:@"%.0fx", speed] : [NSString stringWithFormat:@"%.2gx", speed];
}

// Called when the map, lane, piano or scroll bar changed the shared window.
- (void)viewportChanged {
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
    const double visibleSec = _map.bounds.size.width / _vp->pxPerSec;
    if (status == ttr::KernelStatus::Recording && recorded > _vp->scrollSec + kFollowFraction * visibleSec)
        _vp->scrollSec = recorded - kFollowFraction * visibleSec;  // the view follows the recording
    TTRClampViewport(_vp.get(), _map.bounds.size);
    _scroll.contentLength = _vp->contentSec + 2.0;
    _scroll.visibleLength = visibleSec;
    _scroll.offset = _vp->scrollSec;
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
    _syncButton.isOn = _kernel->syncToHost();
    [self updateEditStates];
    _soloButton.isOn = _kernel->param(ttr::kParamSolo) >= 0.5f;
    _hideDrumsButton.isOn = _kernel->param(ttr::kParamHideDrums) >= 0.5f;
    const ttr::LineView lineView = _kernel->lineView();
    _fundamentalsButton.isOn = lineView.fundamentals;
    _alignButton.isOn = lineView.alignToSample;
    _tuningReadout.value = [self tuningText];
    _tuningReadout.isHighlighted = lineView.alignToSample && _kernel->detectedTuningCents() != 0.0;
    _tuningMinus.isEnabled = lineView.trimCents > -ttr::kMaxTrimCents;
    _tuningPlus.isEnabled = lineView.trimCents < ttr::kMaxTrimCents;
    _magnetButton.isOn = _kernel->param(ttr::kParamMagnet) >= 0.5f;
    for (const auto& pair : std::vector<std::pair<TTRKnob_v1*, ttr::ParamId>>{{_gainKnob, ttr::kParamGain}, {_rangeKnob, ttr::kParamRange}, {_quickGrabKnob, ttr::kParamQuickGrab},
                                                                              {_soundKnob, ttr::kParamSoundVolume}, {_mixKnob, ttr::kParamMix}})
        if (!pair.first.isDragging) pair.first.value = _kernel->param(pair.second);
    _snap.selectedIndex = NSInteger(_kernel->param(ttr::kParamSnap));
    _speedReadout.value = formatSpeed(_kernel->param(ttr::kParamSpeed));

    const ttr::KeyResult key = snap ? snap->key : ttr::KeyResult{};
    _keyReadout.value = recording ? @"-" : [NSString stringWithUTF8String:ttr::keyName(key).c_str()];
    _keyReadout.isHighlighted = key.root >= 0 && !recording;
    _bpmReadout.value = formatBpm(snap && snap->hasRecording() ? snap->bpm : _kernel->hostBpm());
    _info.lines = [self infoLines];

    NSString* text = [self statusLineForSnapshot:snap status:status];
    if (![text isEqualToString:_statusText]) {
        _statusText = text;
        [self setNeedsDisplayInRect:NSMakeRect(0, self.bounds.size.height - kTTRStatusHeight, self.bounds.size.width - kTTRPanelWidth, kTTRStatusHeight)];
    }
    [self pollNotices];
    [self updateDiagnostics];
    [self storeUiSettingsIfChanged];
    [self updateTimer];
}

@end
