// Construction and layout of the main view (1280 x 800 by default, 1024 x 640 at the minimum):
//   top bar  | logo · rewind play follow record · time · model · ORIG/MIDI · volume · mute · options
//   sidebar  | INSTRUMENTS + one strip per instrument, MASTER meter at the bottom
//   main     | song title · export tempo · Export MIDI out · Drag MIDI out · clear
//            | waveform lane with the ruler, piano roll, scroll bar, status line with the zoom slider
#include <algorithm>
#include <cmath>

#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

const CGFloat kTTRTopBarHeight = 72, kTTRSidebarWidth = 300, kTTRSongHeaderHeight = 64, kTTRWaveHeight = 168, kTTRStatusHeight = 36, kTTRScrollHeight = 10,
              kTTRMasterHeight = 64;
static const CGFloat kPad = 24, kGap = 8, kButton = 40, kStripTop = 53;

@implementation TTRMainView_v1 (Build)

- (TTRGlyphButton_v1*)addGlyph:(TTRTransportGlyph_v1)glyph title:(NSString*)title tip:(NSString*)tip action:(void (^)(void))action {
    TTRGlyphButton_v1* b = [[TTRGlyphButton_v1 alloc] initWithGlyph:glyph title:title];
    b.toolTip = tip;
    b.onClick = action;
    [self addSubview:b];
    return b;
}

- (TTRLineSlider_v1*)addParamSlider:(int)paramId {
    TTRLineSlider_v1* s = [[TTRLineSlider_v1 alloc] initWithFrame:NSZeroRect];
    s.defaultValue = ttr::paramSpec(paramId).defaultValue;
    auto bridge = _bridge;
    s.onBeginEdit = ^{ bridge->beginEdit(paramId); };
    s.onChange = ^(double v) { bridge->setParameter(paramId, float(v)); };
    s.onEndEdit = ^{ bridge->endEdit(paramId); };
    [self addSubview:s];
    return s;
}

- (void)buildTopBar {
    __weak TTRMainView_v1* weak = self;
    _logo = [[TTRLogoView_v1 alloc] initWithFrame:NSZeroRect];
    _logo.toolTip = @"About Tonetrace";
    _logo.onClick = ^{ [weak showAbout]; };
    [self addSubview:_logo];
    _rewindButton = [self addGlyph:TTRTransportRewind title:nil tip:@"Back to the start (Return)" action:^{ [weak rewind]; }];
    _playButton = [self addGlyph:TTRTransportPlay title:nil tip:@"Play / pause the song and the notes (Space) - works while the transcription is still running"
                          action:^{ [weak togglePlay]; }];
    _followButton = [self addGlyph:TTRTransportFollow title:nil tip:@"Follow the playhead" action:^{
        TTRMainView_v1* self = weak;
        if (!self) return;
        self->_tl->follow = !self->_tl->follow;
        [self refreshNow];
    }];
    _recordButton = [self addGlyph:TTRTransportRecord title:nil tip:@"Record the audio playing through this track; stop to transcribe it" action:^{ [weak toggleRecord]; }];
    _modelPill = [[TTRPill_v1 alloc] initWithFrame:NSZeroRect];
    _modelPill.toolTip = @"Transcription model (MuScriptor medium, runs on this Mac). Downloads 620 MB once on first use; weights CC BY-NC 4.0";
    [self addSubview:_modelPill];
    _mixSlider = [self addParamSlider:ttr::kParamMix];
    _mixSlider.leftLabel = @"ORIG";
    _mixSlider.rightLabel = @"MIDI";
    _mixSlider.toolTip = @"Playback balance: the original song (left) and the transcribed notes (right)";
    _volumeSlider = [self addParamSlider:ttr::kParamVolume];
    _volumeSlider.valueText = ^NSString*(double v) {
        const float db = ttr::faderToDb(float(v));
        return std::isfinite(db) ? [NSString stringWithFormat:@"%.1f", db] : @"-inf";
    };
    _volumeSlider.toolTip = @"Playback volume (double-click: 0 dB)";
    _volumeSlider.leftLabel = @"VOL";
    _muteButton = [self addGlyph:TTRTransportSpeaker title:@"MUTE" tip:@"Silence the track's own audio in the output (hear only Tonetrace's playback)" action:^{
        TTRMainView_v1* self = weak;
        if (!self) return;
        [self setParam:ttr::kParamMuteInput value:self->_kernel->param(ttr::kParamMuteInput) >= 0.5f ? 0.0f : 1.0f];
        [self refreshNow];
    }];
    _muteButton.isWarning = YES;
    _optionsButton = [self addGlyph:TTRTransportOptions title:nil tip:@"Diagnostics: version, host, load, model status, log folder" action:^{ [weak toggleDiagnostics]; }];
}

- (void)buildMainArea {
    __weak TTRMainView_v1* weak = self;
    _tempoStepper = [[TTRStepper_v1 alloc] initWithFrame:NSZeroRect];
    _tempoStepper.title = @"EXPORT TEMPO";
    _tempoStepper.toolTip = @"Tempo of the exported MIDI file (the song's tempo is not detected): drag, scroll or click the arrows";
    auto kernel = _kernel;
    _tempoStepper.onChange = ^(double v) { kernel->setExportBpm(v); };
    [self addSubview:_tempoStepper];
    _exportButton = [self addGlyph:TTRTransportFolder title:@"Export MIDI out" tip:@"Save the MIDI file (one track per audible instrument) (Cmd+E)"
                            action:^{ [weak exportMidiWithPanel]; }];
    _dragButton = [[TTRGlyphDragButton_v1 alloc] initWithGlyph:TTRTransportDownload title:@"Drag MIDI out"];
    _dragButton.isAccent = YES;
    _dragButton.toolTip = @"Drag the MIDI file into your DAW: one track per audible instrument, drums on channel 10";
    _dragButton.dragFileProvider = ^NSURL*(void) { return [weak writeMidiToMusicFolder]; };
    [self addSubview:_dragButton];
    _clearButton = [self addGlyph:TTRTransportTrash title:nil tip:@"Remove the song and its notes" action:^{ [weak clearSong]; }];
    _openButton = [self addGlyph:TTRTransportFolder title:@"Open song…" tip:@"Choose an audio file (Cmd+O)" action:^{ [weak openSongPanel]; }];

    _wave = [[TTRWaveLane_v1 alloc] initWithTimeline:_tl];
    _wave.onSeek = ^(double sec) { [weak seekTo:sec]; };
    _wave.onWheel = ^(NSEvent* e, CGFloat x) { [weak handleWheel:e noteAreaX:x]; };
    _roll = [[TTRPianoRoll_v1 alloc] initWithTimeline:_tl];
    _roll.onSeek = ^(double sec) { [weak seekTo:sec]; };
    _roll.onWheel = ^(NSEvent* e, CGFloat x) { [weak handleWheel:e noteAreaX:x]; };
    _roll.onMagnify = ^(NSEvent* e, CGFloat x) { [weak handleMagnify:e noteAreaX:x]; };
    _scroll = [[TTRScrollBar_v1 alloc] initWithFrame:NSZeroRect];
    _scroll.onScroll = ^(double offset) {
        TTRMainView_v1* self = weak;
        if (!self) return;
        self->_tl->isFit = false;
        self->_tl->scrollSec = offset;
        [self refreshNow];
    };
    _zoomSlider = [[TTRLineSlider_v1 alloc] initWithFrame:NSZeroRect];
    _zoomSlider.toolTip = @"Time zoom (Cmd+wheel or pinch on the notes); double-click shows the whole song";
    _zoomSlider.defaultValue = 0.0;
    _zoomSlider.trackColor = 0x6A707B;
    _zoomSlider.onChange = ^(double v) {
        TTRMainView_v1* self = weak;
        if (!self) return;
        if (v <= 0.0) { self->_tl->isFit = true; [self refreshNow]; return; }
        [self zoomTo:ttr::kMinPixelsPerSecond * std::pow(ttr::kMaxPixelsPerSecond / ttr::kMinPixelsPerSecond, v) aroundX:-1];
    };
    for (NSView* v in @[ _wave, _roll, _scroll, _zoomSlider ]) [self addSubview:v];
}

- (void)buildSidebar {
    _strips = [[NSView alloc] initWithFrame:NSZeroRect];
    _strips.wantsLayer = YES;
    _strips.layer.masksToBounds = YES;
    [self addSubview:_strips];
    _masterMeter = [[TTRMeter_v1 alloc] initWithFrame:NSZeroRect];
    _masterMeter.toolTip = @"Output level of the plug-in";
    [self addSubview:_masterMeter];
}

- (void)buildSubviews {
    [self buildTopBar];
    [self buildSidebar];
    [self buildMainArea];
    __weak TTRMainView_v1* weak = self;
    _banner = [[TTRBannerView_v1 alloc] initWithFrame:NSZeroRect];
    _diag = [[TTRDiagnosticsView_v1 alloc] initWithFrame:NSZeroRect];
    _diag.onClose = ^{ [weak toggleDiagnostics]; };
    _diag.onMessage = ^(NSString* m) { [weak showMessage:m]; };
    _about = [[TTRAboutView_v1 alloc] initWithFrame:NSZeroRect];
    _about.onOpenDiagnostics = ^{ [weak toggleDiagnostics]; };
    for (NSView* v in @[ _banner, _diag, _about ]) [self addSubview:v];  // overlays on top
}

@end

@implementation TTRMainView_v1 (Layout)

- (NSRect)songHeaderRect { return NSMakeRect(kTTRSidebarWidth, kTTRTopBarHeight, self.bounds.size.width - kTTRSidebarWidth, kTTRSongHeaderHeight); }

- (NSRect)statusRect {
    const NSRect b = self.bounds;
    return NSMakeRect(kTTRSidebarWidth, b.size.height - kTTRStatusHeight, b.size.width - kTTRSidebarWidth, kTTRStatusHeight);
}

- (NSRect)noteAreaRect {
    const NSRect header = [self songHeaderRect], status = [self statusRect];
    return NSMakeRect(header.origin.x, NSMaxY(header), header.size.width, status.origin.y - NSMaxY(header));
}

// Left to right: logo, transport, time; right to left: options, MUTE, volume, ORIG/MIDI; the model pill takes what is
// left in the middle and is hidden when the window is too narrow (the status line names the model anyway).
- (void)layoutTopBar:(CGFloat)W {
    const CGFloat y = (kTTRTopBarHeight - kButton) / 2;
    _logo.frame = NSMakeRect(kPad, y + 7, 150, 26);
    CGFloat x = kPad + 150 + 20;
    for (TTRGlyphButton_v1* b in @[ _rewindButton, _playButton, _followButton, _recordButton ]) {
        b.frame = NSMakeRect(x, y, kButton, kButton);
        x += kButton + 6;
    }
    x += 12;
    _timeRect = NSMakeRect(x, y, 206, kButton);
    x += 206 + 12;
    const BOOL narrow = W < 1260, tight = W < 1150;
    CGFloat right = W - kPad;
    _optionsButton.frame = NSMakeRect(right - kButton, y, kButton, kButton);
    right -= kButton + kGap;
    const CGFloat muteW = tight ? kButton : 104;  // a tight window shows the speaker only
    _muteButton.title = tight ? nil : @"MUTE";
    _muteButton.frame = NSMakeRect(right - muteW, y, muteW, kButton);
    right -= muteW + 12;
    const CGFloat volumeW = tight ? 110 : narrow ? 120 : 150;
    _volumeSlider.frame = NSMakeRect(right - volumeW, y, volumeW, kButton);
    right -= volumeW + 16;
    const CGFloat mixW = tight ? 140 : narrow ? 150 : 176;
    _mixSlider.frame = NSMakeRect(right - mixW, y, mixW, kButton);
    right -= mixW + 16;
    _modelPill.hidden = right - x < 120;
    _modelPill.frame = NSMakeRect(x, y, std::min<CGFloat>(170, right - x), kButton);
}

- (void)layoutSidebar:(CGFloat)H {
    const CGFloat top = kTTRTopBarHeight + kStripTop;
    const CGFloat masterY = H - kTTRMasterHeight;
    _strips.frame = NSMakeRect(0, top, kTTRSidebarWidth - 1, std::max<CGFloat>(0, masterY - top));
    _masterMeter.frame = NSMakeRect(kPad, masterY + 40, kTTRSidebarWidth - 2 * kPad, 6);
    const CGFloat contentH = CGFloat(_stripByProgram.size()) * kTTRStripHeight;
    _stripScroll = std::clamp<CGFloat>(_stripScroll, 0, std::max<CGFloat>(0, contentH - _strips.frame.size.height));
    CGFloat y = _strips.frame.size.height - kTTRStripHeight + _stripScroll;  // the container is not flipped: build from the top
    for (const auto& entry : _stripByProgram) {
        entry.second.frame = NSMakeRect(0, y, kTTRSidebarWidth - 1, kTTRStripHeight);
        y -= kTTRStripHeight;
    }
}

- (void)layoutMainArea {
    const NSRect header = [self songHeaderRect], area = [self noteAreaRect], status = [self statusRect];
    const CGFloat y = header.origin.y + (header.size.height - kButton) / 2;
    const BOOL compact = self.bounds.size.width < 1200;  // the song title keeps some room
    CGFloat right = NSMaxX(header) - kPad;
    _clearButton.frame = NSMakeRect(right - kButton, y, kButton, kButton);
    right -= kButton + kGap;
    const CGFloat dragW = compact ? 160 : 178, exportW = compact ? kButton : 182, tempoW = compact ? 176 : 196;
    _dragButton.frame = NSMakeRect(right - dragW, y, dragW, kButton);
    right -= dragW + kGap;
    _exportButton.title = compact ? nil : @"Export MIDI out";
    _exportButton.frame = NSMakeRect(right - exportW, y, exportW, kButton);
    right -= exportW + kGap;
    _tempoStepper.frame = NSMakeRect(right - tempoW, y, tempoW, kButton);
    const CGFloat waveH = std::min(kTTRWaveHeight, area.size.height * 0.32);
    _wave.frame = NSMakeRect(area.origin.x, area.origin.y, area.size.width, waveH);
    const CGFloat rollY = NSMaxY(_wave.frame);
    _roll.frame = NSMakeRect(area.origin.x, rollY, area.size.width, std::max<CGFloat>(40, NSMaxY(area) - rollY - kTTRScrollHeight));
    _scroll.frame = NSMakeRect(area.origin.x + kTTRKeyboardWidth, NSMaxY(_roll.frame), area.size.width - kTTRKeyboardWidth, kTTRScrollHeight);
    _zoomSlider.frame = NSMakeRect(NSMaxX(status) - 200, status.origin.y + 4, 180, status.size.height - 8);
    _openButton.frame = NSMakeRect(NSMidX(area) - 90, NSMidY(area) + 6, 180, kButton);
}

- (void)layoutControls {
    const CGFloat W = self.bounds.size.width, H = self.bounds.size.height;
    [self layoutTopBar:W];
    [self layoutSidebar:H];
    [self layoutMainArea];
    const NSRect area = [self noteAreaRect];
    _banner.frame = NSMakeRect(area.origin.x + 16, area.origin.y + 12, MAX(60, area.size.width - 32), 40);
    _diag.frame = NSMakeRect(kTTRSidebarWidth, kTTRTopBarHeight, W - kTTRSidebarWidth, H - kTTRTopBarHeight);
    _about.frame = self.bounds;
}

@end
