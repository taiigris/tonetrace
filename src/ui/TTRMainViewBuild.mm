// Construction of the main view: header icons, map area and the controls of the side column and of the options popover.
#import "core/NoteEdit.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

@implementation TTRMainView_v1 (Build)

- (TTRButton_v1*)addButton:(NSString*)title kind:(TTRButtonKind_v1)kind tip:(NSString*)tip action:(void (^)(void))action {
    TTRButton_v1* b = [[TTRButton_v1 alloc] initWithTitle:title kind:kind];
    b.toolTip = tip;
    b.onClick = action;
    [self addSubview:b];
    return b;
}

- (TTRButton_v1*)addIcon:(NSString*)name glyph:(TTRIconGlyph_v1)glyph tip:(NSString*)tip action:(void (^)(void))action {
    TTRButton_v1* b = [self addButton:name kind:TTRButtonKindIcon tip:tip action:action];
    b.glyph = glyph;
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
    TTRKnob_v1* knob = [[TTRKnob_v1 alloc] initWithFrame:NSMakeRect(0, 0, 80, 64)];
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

- (TTRReadout_v1*)makeReadout:(NSString*)title tip:(NSString*)tip stacked:(BOOL)stacked {
    TTRReadout_v1* r = [[TTRReadout_v1 alloc] initWithFrame:NSZeroRect];
    r.title = title;
    r.toolTip = tip;
    r.isStacked = stacked;
    return r;
}

// Map, lane, piano and scroll bar.
- (void)buildMapArea {
    __weak TTRMainView_v1* weak = self;
    auto kernel = _kernel;
    _wave = [[TTRWaveformView_v1 alloc] initWithKernel:_kernel viewport:_vp];
    _map = [[TTRNoteMapView_v1 alloc] initWithKernel:_kernel viewport:_vp];
    _piano = [[TTRPianoKeys_v1 alloc] initWithViewport:_vp];
    _scroll = [[TTRScrollBar_v1 alloc] initWithFrame:NSZeroRect];
    void (^zoom)(double) = ^(double speed) {
        TTRMainView_v1* self = weak;
        if (!self) return;
        self->_fitZoom = NO;
        [self editParam:ttr::kParamSpeed value:float(speed)];
    };
    _wave.toolTip = @"Waveform: click sets the preview start, drag selects a loop region, wheel scrolls and zooms";
    _map.toolTip = @"Note map: click or drag anywhere to draw a note, drag a note to move it to any row and time, drag its ends to resize, right-click for the menu";
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
}

- (void)buildHeader {
    __weak TTRMainView_v1* weak = self;
    auto kernel = _kernel;
    _helpButton = [self addIcon:@"?" glyph:TTRIconHelp tip:@"Help: the most useful gestures and shortcuts" action:^{ [weak showHelp]; }];
    _snap = [[TTRDropdown_v1 alloc] initWithFrame:NSZeroRect];
    _snap.title = @"Snap";
    NSMutableArray* names = [NSMutableArray array];
    for (int i = 0; i < ttr::kNumSnapModes; ++i) [names addObject:[NSString stringWithUTF8String:ttr::snapName(i)]];
    _snap.items = names;
    _snap.toolTip = @"Snap: Onset (default) snaps note starts and ends to detected onsets and line ends; 1/4 to 1/32 snap to the grid (also for the exported MIDI); Off is free. Hold Alt to draw without snap";
    _snap.onChange = ^(NSInteger i) { [weak editParam:ttr::kParamSnap value:float(i)]; };
    [self addSubview:_snap];
    _viewButton = [self addIcon:@"View" glyph:TTRIconView tip:@"View: the full spectrogram (default) or only the tracked spectral peaks as thin lines" action:^{
        ttr::LineView v = kernel->lineView();
        v.isSpectrum = !v.isSpectrum;
        [weak lineViewChanged:v];
    }];
    _optionsButton = [self addIcon:@"Options" glyph:TTRIconGear tip:@"Options: Hide drums, Fundamentals, Magnet, Grab on click, Align, Tuning, Wave, Sync, Sound, Mix, Solo, Live bridge" action:^{ [weak toggleOptions]; }];
    _infoButton = [self addIcon:@"ⓘ" glyph:TTRIconInfo tip:@"Diagnostics: version, host, load, log folder" action:^{ [weak toggleDiagnostics]; }];
    _logoView = [[TTRLogoView_v1 alloc] initWithFrame:NSZeroRect];
    _logoView.toolTip = @"About Tonetrace";
    _logoView.onClick = ^{ [weak showAbout]; };
    [self addSubview:_logoView];
}

// Right column.
- (void)buildSidebar {
    __weak TTRMainView_v1* weak = self;
    auto kernel = _kernel;
    auto bridge = _bridge;
    NSString * (^percent)(double) = ^NSString*(double v) { return [NSString stringWithFormat:@"%.0f%%", v * 100]; };
    _gainKnob = [self addKnob:@"Gain" param:ttr::kParamGain tip:@"Brightness of the spectrum" format:^NSString*(double v) { return [NSString stringWithFormat:@"%+.0f dB", (v - 0.5) * 40]; }];
    _rangeKnob = [self addKnob:@"Range" param:ttr::kParamRange tip:@"Dynamic range of the map: lower = only the strongest lines" format:^NSString*(double v) { return [NSString stringWithFormat:@"%.0f dB", 18 + v * 50]; }];
    _speedMinus = [self addButton:@"−" kind:TTRButtonKindNormal tip:@"Zoom out in time" action:^{ [weak stepSpeed:-1]; }];
    _speedPlus = [self addButton:@"+" kind:TTRButtonKindNormal tip:@"Zoom in in time" action:^{ [weak stepSpeed:1]; }];
    _speedReadout = [self makeReadout:@"Speed" tip:@"Time zoom: Fit shows the whole recording; − and + zoom from there (Cmd+wheel or pinch)" stacked:YES];
    [self addSubview:_speedReadout];
    _quickGrabSlider = [[TTRSlider_v1 alloc] initWithFrame:NSZeroRect];
    _quickGrabSlider.title = @"Quick Grab";
    _quickGrabSlider.defaultValue = ttr::paramSpec(ttr::kParamQuickGrab).defaultValue;
    _quickGrabSlider.valueFormatter = percent;
    _quickGrabSlider.toolTip = @"How far below the strongest line Quick Grab still picks up notes (also the sensitivity of the detection)";
    _quickGrabSlider.onBeginEdit = ^{ bridge->beginEdit(ttr::kParamQuickGrab); };
    _quickGrabSlider.onChange = ^(double v) { bridge->setParameter(ttr::kParamQuickGrab, float(v)); };
    _quickGrabSlider.onEndEdit = ^{ bridge->endEdit(ttr::kParamQuickGrab); };
    [self addSubview:_quickGrabSlider];
    _grabButton = [self addButton:@"Grab" kind:TTRButtonKindNormal tip:@"Create notes from the strongest lines as a starting point" action:^{ [weak grabClicked]; }];
    _recordButton = [[TTRRecordButton_v1 alloc] initWithFrame:NSZeroRect];
    _recordButton.toolTip = @"Record the audio passing through this plugin";
    _recordButton.onClick = ^{ [weak toggleRecord]; };
    [self addSubview:_recordButton];
    _resetButton = [self addButton:@"Reset" kind:TTRButtonKindNormal tip:@"Delete the recording and all notes (Cmd-click skips the question)" action:^{ [weak resetClicked]; }];
    _previewButton = [self addButton:@"Preview" kind:TTRButtonKindPlay tip:@"Play the recording together with your notes (Space)" action:^{ [weak togglePreview]; }];
    _dragButton = [[TTRDragButton_v1 alloc] initWithTitle:@"Drag MIDI" kind:TTRButtonKindDrag];
    _dragButton.toolTip = @"Drag the selected notes (or all notes) into a MIDI track or clip";
    _dragButton.dragFileProvider = ^NSURL*(void) { return [weak writeNotesToMidiFile]; };
    _dragButton.onDragRejected = ^{ [weak showMessage:@"Draw at least one note, then drag"]; };
    [self addSubview:_dragButton];
    _saveButton = [self addButton:@"Save .mid" kind:TTRButtonKindNormal tip:@"Save the selected notes (or all notes) as a MIDI file" action:^{ [weak saveNotesAsMidi]; }];
    _samplerButton = [self addButton:@"New Sampler Track" kind:TTRButtonKindNormal tip:@"Ableton Live: create a MIDI track with a Simpler and a clip with the selected notes (or all) through the Tonetrace Bridge" action:^{ [weak samplerClicked]; }];
    _undoButton = [self addButton:@"Undo" kind:TTRButtonKindNormal tip:@"Undo the last edit, Quick Grab, Clear or Reset (Cmd+Z)" action:^{ [weak undoClicked]; }];
    _redoButton = [self addButton:@"Redo" kind:TTRButtonKindNormal tip:@"Redo (Cmd+Shift+Z)" action:^{ [weak redoClicked]; }];
    _info = [[TTRInfoBox_v1 alloc] initWithFrame:NSZeroRect];
    [self addSubview:_info];
    (void)kernel;
}

// Everything secondary lives in the options popover.
- (void)buildOptions {
    __weak TTRMainView_v1* weak = self;
    auto kernel = _kernel;
    NSString * (^percent)(double) = ^NSString*(double v) { return [NSString stringWithFormat:@"%.0f%%", v * 100]; };
    _hideDrumsButton = [self addToggle:@"Hide drums" param:ttr::kParamHideDrums tip:@"Dim drum hits in the spectrum to 30 % brightness (nothing is removed). Quick Grab always leaves drums out"];
    _fundamentalsButton = [self addButton:@"Fundamentals" kind:TTRButtonKindNormal tip:@"Fundamentals: dim the overtones of stronger, lower lines to 30 % brightness, so mostly the fundamental of every note stays bright"
                                      action:^{ ttr::LineView v = kernel->lineView(); v.fundamentals = !v.fundamentals; [weak lineViewChanged:v]; }];
    _magnetButton = [self addToggle:@"Magnet" param:ttr::kParamMagnet tip:@"New notes you draw snap to the strongest line within one semitone of the pointer (Alt flips it for one gesture). Moving notes is always free"];
    _grabOnClickButton = [self addButton:@"Grab on click" kind:TTRButtonKindNormal tip:@"A click exactly on a strong line grabs the whole line as a note (off: a click always draws a note of the snap length)"
                                  action:^{ ttr::LineView v = kernel->lineView(); v.grabOnClick = !v.grabOnClick; [weak lineViewChanged:v]; }];
    _alignButton = [self addButton:@"Align" kind:TTRButtonKindNormal tip:@"Align to sample: shifts the semitone rows by the sample's detected tuning, so the lines sit in the middle of their rows (notes get the right MIDI pitch)"
                               action:^{ ttr::LineView v = kernel->lineView(); v.alignToSample = !v.alignToSample; [weak lineViewChanged:v]; }];
    _waveButton = [self addButton:@"Wave" kind:TTRButtonKindNormal tip:@"Show or hide the waveform lane above the map (the time ruler stays)" action:^{ [weak toggleWave]; }];
    _waveButton.isOn = YES;
    _syncButton = [self addButton:@"Sync" kind:TTRButtonKindNormal tip:@"Sync to host: while the host transport runs, the preview follows the song position (relative to where you recorded)" action:^{ [weak syncClicked]; }];
    _samplerDragButton = [self addButton:@"Sampler on Drag" kind:TTRButtonKindNormal tip:@"After a Drag MIDI also create the sampler track in Live (needs the Tonetrace Bridge)"
                                  action:^{ ttr::LineView v = kernel->lineView(); v.samplerOnDrag = !v.samplerOnDrag; [weak lineViewChanged:v]; }];
    _tuningMinus = [self addButton:@"−" kind:TTRButtonKindNormal tip:@"Tuning trim -5 cent (move the rows down)" action:^{ [weak nudgeTuning:-1]; }];
    _tuningPlus = [self addButton:@"+" kind:TTRButtonKindNormal tip:@"Tuning trim +5 cent (move the rows up), at most 50 cent" action:^{ [weak nudgeTuning:1]; }];
    _tuningReadout = [self makeReadout:@"Tuning" tip:@"Detected deviation of the sample from A = 440 Hz (plus your trim). The rows follow it while Align is on" stacked:NO];
    [self addSubview:_tuningReadout];
    _soundKnob = [self addKnob:@"Sound" param:ttr::kParamSoundVolume tip:@"Volume of the note synth in the preview" format:percent];
    _mixKnob = [self addKnob:@"Mix" param:ttr::kParamMix tip:@"Preview balance: recording (left) and notes (right)" format:^NSString*(double v) { return v < 0.02 ? @"Sample" : v > 0.98 ? @"Notes" : [NSString stringWithFormat:@"%.0f%%", v * 100]; }];
    _soloButton = [self addToggle:@"Solo" param:ttr::kParamSolo tip:@"Mute the incoming signal while the preview plays"];
    _clearButton = [self addButton:@"Clear notes" kind:TTRButtonKindNormal tip:@"Remove every note (can be undone)" action:^{ kernel->editNotes({}); }];
    _deleteButton = [self addButton:@"Delete" kind:TTRButtonKindNormal tip:@"Delete the selected notes (Backspace, right-click menu)" action:^{ [weak deleteClicked]; }];
    _duplicateButton = [self addButton:@"Duplicate" kind:TTRButtonKindNormal tip:@"Duplicate the selected notes behind the selection (Cmd+D)" action:^{ [weak duplicateClicked]; }];
    _bridgeReadout = [self makeReadout:@"Live bridge" tip:@"Tonetrace Bridge: a Live MIDI Remote Script. Connected = it answered within the last 3 seconds (see the README for the set-up)" stacked:NO];
    [self addSubview:_bridgeReadout];
    _options = [[TTRPopoverView_v1 alloc] initWithFrame:NSZeroRect];
    _options.hidden = YES;
    for (NSView* v in @[ _hideDrumsButton, _fundamentalsButton, _magnetButton, _grabOnClickButton, _alignButton, _waveButton, _syncButton, _samplerDragButton, _tuningMinus, _tuningReadout,
                         _tuningPlus, _soundKnob, _mixKnob, _soloButton, _clearButton, _deleteButton, _duplicateButton, _bridgeReadout ])
        [_options addSubview:v];  // moves them out of the main view
}

- (void)buildSubviews {
    __weak TTRMainView_v1* weak = self;
    [self buildMapArea];
    [self buildHeader];
    [self buildSidebar];
    [self buildOptions];
    _banner = [[TTRBannerView_v1 alloc] initWithFrame:NSZeroRect];
    _diag = [[TTRDiagnosticsView_v1 alloc] initWithFrame:NSZeroRect];
    _diag.onClose = ^{ [weak toggleDiagnostics]; };
    _diag.onMessage = ^(NSString* m) { [weak showMessage:m]; };
    _diag.toolTip = @"Diagnostics";
    _about = [[TTRAboutView_v1 alloc] initWithFrame:NSZeroRect];  // topmost: dims the whole editor
    _about.onOpenDiagnostics = ^{ [weak openDiagnostics]; };
    _about.onOpenLogFolder = ^{ [weak openLogFolder]; };
    _keyReadout = [self makeReadout:@"Key" tip:nil stacked:YES];  // not views: their values feed the header status
    _bpmReadout = [self makeReadout:@"BPM" tip:nil stacked:YES];
    for (NSView* v in @[ _banner, _diag, _options, _about ]) [self addSubview:v];  // overlays, the About card covers every other view
}

@end
