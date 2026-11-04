// The main view: one refresh tick reads the kernel (document, transport, levels) and updates every part; the view itself
// draws the bars between the parts, the time display, the headers, the status line and the drop zone.
#import "ui/TTRMainView.h"

#include <algorithm>
#include <cmath>

#include "analysis/ml/MuScriptorEngine.h"
#include "core/Instruments.h"
#include "core/SongImport.h"
#include "midi/ExportPlanner.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

const NSSize kTTRDefaultSize = {1280, 800};
const NSSize kTTRMinimumSize = {1024, 640};
const double kTTRActiveHz = 30.0, kTTRIdleHz = 5.0;
static const CFTimeInterval kMessageSeconds = 4.0;
static const double kFollowEdge = 0.85, kFollowJump = 0.1;  // the view jumps when the playhead passes 85 % of it

static NSString* clockText(double sec) {
    const double s = std::max(0.0, sec);
    const int minutes = int(s) / 60;
    return [NSString stringWithFormat:@"%02d:%05.2f", minutes, s - minutes * 60.0];
}

@implementation TTRMainView_v1

- (instancetype)initWithBridge:(std::shared_ptr<ttr::TTRHostBridge>)bridge {
    self = [super initWithFrame:NSMakeRect(0, 0, kTTRDefaultSize.width, kTTRDefaultSize.height)];
    if (!self) return nil;
    _bridge = std::move(bridge);
    _kernel = _bridge->kernel();
    _tl = std::make_shared<TTRTimeline>();
    _shownDocId = UINT64_MAX;
    self.wantsLayer = YES;
    self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    self.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
    [self buildSubviews];
    [self restoreUiSettings];
    [self layoutControls];
    [self refreshNow];
    return self;
}

- (void)dealloc { [_timer invalidate]; }
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }

- (void)shutdown {
    [_timer invalidate];
    _timer = nil;
}

- (void)viewDidMoveToWindow {
    [super viewDidMoveToWindow];
    _isWindowVisible = self.window != nil;
    if (self.window) [self.window makeFirstResponder:self];
    [self updateTimer];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    [self layoutControls];
}

- (void)showMessage:(NSString*)message {
    _transientMessage = [message copy];
    _transientUntil = CACurrentMediaTime() + kMessageSeconds;
    [self setNeedsDisplayInRect:[self statusRect]];
}

// ------------------------------------------------------------ keys

- (void)keyDown:(NSEvent*)event {
    NSString* chars = event.charactersIgnoringModifiers;
    const BOOL cmd = (event.modifierFlags & NSEventModifierFlagCommand) != 0;
    if ([chars isEqualToString:@" "]) { [self togglePlay]; return; }
    if (event.keyCode == 36 || event.keyCode == 115) { [self rewind]; return; }  // Return, Home
    if (cmd && [chars.lowercaseString isEqualToString:@"o"]) { [self openSongPanel]; return; }
    if (cmd && [chars.lowercaseString isEqualToString:@"e"]) { [self exportMidiWithPanel]; return; }
    [super keyDown:event];
}

// ------------------------------------------------------------ dropping a song file

static NSString* songPathInPasteboard(NSPasteboard* board) {
    NSArray<NSURL*>* urls = [board readObjectsForClasses:@[ NSURL.class ] options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
    for (NSURL* url in urls)
        if (url.path && ttr::isSongFile(url.path.UTF8String)) return url.path;
    return nil;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    _isDropTarget = songPathInPasteboard(sender.draggingPasteboard) != nil;
    self.needsDisplay = YES;
    return _isDropTarget ? NSDragOperationCopy : NSDragOperationNone;
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender { return songPathInPasteboard(sender.draggingPasteboard) ? NSDragOperationCopy : NSDragOperationNone; }
- (void)draggingExited:(id<NSDraggingInfo>)sender { _isDropTarget = NO; self.needsDisplay = YES; }
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    _isDropTarget = NO;
    self.needsDisplay = YES;
    NSString* path = songPathInPasteboard(sender.draggingPasteboard);
    if (!path) return NO;
    [self importSongAtPath:path];
    return YES;
}

// ------------------------------------------------------------ refresh

static const CGFloat kTTRFullModelPill = 118;  // narrower: "MEDIUM" instead of "MODEL: MEDIUM"

- (NSString*)modelPillText:(uint32_t*)color {
    const std::string status = ttr::transcriberStatus();
    *color = 0;
    if (status.rfind("model downloading", 0) == 0) {
        const size_t open = status.find('('), percent = status.find('%');
        *color = 0x9DB8FF;
        if (open != std::string::npos && percent != std::string::npos && percent > open)
            return [NSString stringWithFormat:@"MODEL: %s%%", status.substr(open + 1, percent - open - 1).c_str()];
        return @"MODEL: DOWNLOADING";
    }
    if (status == "ready" || status == "not loaded yet" || status == "model not downloaded yet")
        return [[NSString stringWithFormat:@"MODEL: %s", ttr::modelSizeInfo(ttr::modelSize()).label] uppercaseString];
    *color = 0xFFB35C;
    return @"MODEL: OFFLINE";
}

// The vertical scroll that centres the notes (or C4 without notes).
- (void)centreRowsOnDocument {
    double sum = 0.0;
    int count = 0;
    if (_doc)
        for (const ttr::NoteEvent& n : _doc->notes)
            if (n.program != ttr::kDrumProgram) { sum += n.pitch; ++count; }
    const double centre = count > 0 ? sum / count : 60.0;
    const double height = _roll.bounds.size.height;
    _tl->scrollY = std::clamp((ttr::kMaxNote + 0.5 - centre) * _tl->rowH - height / 2.0, 0.0, std::max(0.0, ttr::kNumNotes * _tl->rowH - height));
}

- (void)updateTimeline:(BOOL)isRecording playhead:(double)playhead {
    const CGFloat width = _roll.bounds.size.width - kTTRKeyboardWidth;
    _tl->contentSec = _doc ? _doc->lengthSec : 0.0;
    if (isRecording) {
        _tl->contentSec = std::max(10.0, _kernel->recordedSeconds() * 1.1);
        _tl->fit(width);
    } else if (_tl->isFit) {
        _tl->fit(width);
    } else if (_kernel->isPlaying() && _tl->follow) {
        const double visible = _tl->visibleSec(width);
        if (playhead > _tl->scrollSec + kFollowEdge * visible || playhead < _tl->scrollSec) _tl->scrollSec = playhead - kFollowJump * visible;
    }
    _tl->clampScroll(width);
    const double maxY = std::max(0.0, ttr::kNumNotes * _tl->rowH - _roll.bounds.size.height);
    _tl->scrollY = std::clamp(_tl->scrollY, 0.0, maxY);
    _scroll.contentLength = std::max(_tl->contentSec, _tl->visibleSec(width));
    _scroll.visibleLength = _tl->visibleSec(width);
    _scroll.offset = _tl->scrollSec;
    if (!_zoomSlider.isDragging) _zoomSlider.value = std::log(_tl->pxPerSec / ttr::kMinPixelsPerSecond) / std::log(ttr::kMaxPixelsPerSecond / ttr::kMinPixelsPerSecond);
}

- (void)updateTransport:(BOOL)isRecording {
    const BOOL hasSong = _doc && _doc->lengthSec > 0.0;
    _playButton.isOn = _kernel->isPlaying();
    _playButton.isEnabled = hasSong && !isRecording;
    _rewindButton.isEnabled = hasSong && !isRecording;
    _recordButton.isOn = isRecording;
    _followButton.isOn = _tl->follow;
    _muteButton.isOn = _kernel->param(ttr::kParamMuteInput) >= 0.5f;
    if (!_mixSlider.isDragging) _mixSlider.value = _kernel->param(ttr::kParamMix);
    if (!_volumeSlider.isDragging) _volumeSlider.value = _kernel->param(ttr::kParamVolume);
    uint32_t pillColor = 0;
    NSString* pillText = [self modelPillText:&pillColor];
    if (_modelPill.frame.size.width < kTTRFullModelPill && [pillText hasPrefix:@"MODEL: "]) pillText = [pillText substringFromIndex:7];  // narrow window
    _modelPill.text = pillText;
    _modelPill.textColor = pillColor;
    const std::vector<int> selection = _kernel->instrumentSelection();
    _instrumentPill.text = selection.empty() ? @"ALL" : [[NSString stringWithUTF8String:ttr::selectionSummary(selection).c_str()] uppercaseString];
    _instrumentPill.textColor = selection.empty() ? 0 : 0x2EE6C5;
    const BOOL hasNotes = _doc && !_doc->notes.empty();
    _dragButton.isEnabled = hasNotes;
    _exportButton.isEnabled = hasNotes;
    _clearButton.isEnabled = _doc != nullptr || _kernel->isWorking();
    _tempoStepper.value = std::round(_kernel->exportBpm() * 10.0) / 10.0;
    _masterMeter.level = _kernel->masterLevel();
}

- (void)refreshNow {
    _doc = _kernel->document();
    const BOOL isRecording = _kernel->isRecording();
    const double playhead = _kernel->positionSec();
    const uint64_t docId = _doc ? _doc->id : 0;
    if (docId != _shownDocId) {  // a new song or recording: fit it and centre the rows
        _shownDocId = docId;
        _tl->isFit = true;
        [self centreRowsOnDocument];
    }
    const BOOL showContent = _doc != nullptr || isRecording;
    _wave.hidden = !showContent;
    _roll.hidden = !showContent || isRecording;
    _scroll.hidden = !showContent || isRecording;
    _openButton.hidden = showContent;
    [self updateTimeline:isRecording playhead:playhead];
    [_wave updateWithDocument:_doc recording:_kernel->recordingView() isRecording:isRecording playhead:playhead];
    if (!_roll.hidden) [_roll updateWithDocument:_doc mixer:_kernel->mixer() playhead:playhead];
    [self updateTransport:isRecording];
    [self rebuildStripsIfNeeded];
    [self updateStrips];
    self.needsDisplay = YES;  // time display, headers, status, drop zone
    [self pollNotices];
    [self updateDiagnostics];
    [self storeUiSettingsIfChanged];
    [self updateTimer];
}

// ------------------------------------------------------------ drawing

- (void)drawTopBar:(NSRect)b {
    [TTRHex(0x1A1C20) setFill];
    NSRectFill(NSMakeRect(0, 0, b.size.width, kTTRTopBarHeight));
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, kTTRTopBarHeight - 1, b.size.width, 1));
    const BOOL isRecording = _kernel->isRecording();
    NSString* now = clockText(isRecording ? _kernel->recordedSeconds() : _kernel->positionSec());
    NSString* total = [NSString stringWithFormat:@"/ %@", clockText(_doc ? _doc->lengthSec : 0.0)];
    NSFont* big = TTRMono(22, NSFontWeightMedium);
    const CGFloat w = TTRTextWidth(now, big);
    TTRDrawText(now, NSMakeRect(_timeRect.origin.x, _timeRect.origin.y, w + 4, _timeRect.size.height), big, isRecording ? TTR_RECORD : TTR_TEXT);
    if (_timeRect.size.width < w + 10 + TTRTextWidth(total, TTRMono(15))) return;  // a narrow window shows the position only
    TTRDrawText(total, NSMakeRect(_timeRect.origin.x + w + 10, _timeRect.origin.y + 2, 140, _timeRect.size.height), TTRMono(15), TTR_DIM);
}

- (void)drawSidebar:(NSRect)b {
    const NSRect side = NSMakeRect(0, kTTRTopBarHeight, kTTRSidebarWidth, b.size.height - kTTRTopBarHeight);
    [TTRHex(0x151619) setFill];
    NSRectFill(side);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(kTTRSidebarWidth - 1, side.origin.y, 1, side.size.height));
    NSRectFill(NSMakeRect(0, side.origin.y + 52, kTTRSidebarWidth, 1));
    NSFont* heading = TTRFont(12, NSFontWeightSemibold);
    TTRDrawText(@"INSTRUMENTS", NSMakeRect(24, side.origin.y, 200, 52), heading, TTR_DIM, NSTextAlignmentLeft, 2.0);
    const int count = int(_stripByProgram.size());
    if (count > 0)
        TTRDrawText([NSString stringWithFormat:@"%d", count], NSMakeRect(24 + TTRTextWidth(@"INSTRUMENTS", heading, 2.0) + 8, side.origin.y, 36, 52), TTRMono(12), TTR_TEXT);
    if (count == 0) {
        NSString* hint = _doc && _doc->transcription.isActive() ? @"Instruments appear here as the\nmodel finds them." : @"Load a song: every instrument the\nmodel finds gets its own strip.";
        [hint drawInRect:NSMakeRect(24, side.origin.y + 72, kTTRSidebarWidth - 48, 60) withAttributes:TTRTextAttrs(TTRFont(12), TTRHex(0x6A707B))];
    }
    const CGFloat masterY = NSMaxY(side) - kTTRMasterHeight;
    [TTRHex(0x151619) setFill];
    NSRectFill(NSMakeRect(0, masterY, kTTRSidebarWidth - 1, kTTRMasterHeight));
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(0, masterY, kTTRSidebarWidth - 1, 1));
    TTRDrawText(@"MASTER", NSMakeRect(24, masterY + 12, 200, 20), TTRFont(12, NSFontWeightSemibold), TTR_DIM, NSTextAlignmentLeft, 2.0);
}

- (void)drawSongHeader {
    const NSRect r = [self songHeaderRect];
    [TTRHex(0x1A1C20) setFill];
    NSRectFill(r);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(r.origin.x, NSMaxY(r) - 1, r.size.width, 1));
    NSString* title = _doc ? [NSString stringWithUTF8String:ttr::songStem(_doc->sourceName).c_str()] : @"No song loaded";
    if (_kernel->isRecording()) title = @"Recording…";
    const CGFloat maxW = _tempoStepper.frame.origin.x - r.origin.x - 40;
    TTRDrawText(title, NSMakeRect(r.origin.x + 24, r.origin.y, maxW, r.size.height), TTRFont(19, NSFontWeightSemibold), _doc ? TTR_TEXT : TTR_DIM);
}

- (void)drawDropZone {
    const NSRect area = [self noteAreaRect];
    [TTRHex(0x17191D) setFill];
    NSRectFill(area);
    const NSRect zone = NSInsetRect(area, 40, 40);
    NSBezierPath* dashed = [NSBezierPath bezierPathWithRoundedRect:zone xRadius:14 yRadius:14];
    const CGFloat pattern[] = {8, 6};
    [dashed setLineDash:pattern count:2 phase:0];
    dashed.lineWidth = 1.5;
    [(_isDropTarget ? TTRHex(0x5B8CFF) : TTRHex(0x3A3F48)) setStroke];
    [dashed stroke];
    if (_isDropTarget) TTRFillRounded(zone, 14, TTRHex(0x5B8CFF, 0.08));
    const CGFloat cy = NSMidY(zone);
    TTRDrawText(@"Drop a song here", NSMakeRect(zone.origin.x, cy - 92, zone.size.width, 34), TTRFont(26, NSFontWeightSemibold), TTR_TEXT, NSTextAlignmentCenter);
    TTRDrawText(@"wav · aiff · mp3 · m4a · flac · caf, up to 10 minutes", NSMakeRect(zone.origin.x, cy - 52, zone.size.width, 20), TTRFont(13), TTR_DIM,
                NSTextAlignmentCenter);
    TTRDrawText(@"Every instrument is transcribed - drums included - and you can play the song while the notes appear.",
                NSMakeRect(zone.origin.x, cy - 30, zone.size.width, 20), TTRFont(13), TTR_DIM, NSTextAlignmentCenter);
    TTRDrawText(@"Or press  ●  to record what plays through this track.", NSMakeRect(zone.origin.x, cy + 76, zone.size.width, 20), TTRFont(12), TTRHex(0x6A707B),
                NSTextAlignmentCenter);
}

- (void)drawRecordingHint {
    const NSRect area = [self noteAreaRect];
    const NSRect roll = NSMakeRect(area.origin.x, NSMaxY(_wave.frame), area.size.width, NSMaxY(area) - NSMaxY(_wave.frame));
    [TTRHex(0x17191D) setFill];
    NSRectFill(roll);
    const double sec = _kernel->recordedSeconds();
    NSString* text = [NSString stringWithFormat:@"Recording %d:%02d - press ● again to stop; the recording is transcribed then", int(sec) / 60, int(sec) % 60];
    TTRDrawText(text, NSMakeRect(roll.origin.x, NSMidY(roll) - 12, roll.size.width, 24), TTRFont(15, NSFontWeightSemibold), TTR_RECORD, NSTextAlignmentCenter);
}

- (NSString*)statusText {
    if (_transientMessage && CACurrentMediaTime() < _transientUntil) return _transientMessage;
    if (!_doc) return _kernel->isRecording() ? @"Recording the input of this track" : @"Drop a song on the window or click Open";
    const std::vector<ttr::InstrumentSummary> instruments = ttr::instrumentsIn(_doc->notes);
    NSMutableArray<NSString*>* parts = [NSMutableArray array];
    [parts addObject:[NSString stringWithFormat:@"%zu instrument%s", instruments.size(), instruments.size() == 1 ? "" : "s"]];
    [parts addObject:[NSString stringWithFormat:@"%zu notes", _doc->notes.size()]];
    [parts addObject:[NSString stringWithFormat:@"%s model", ttr::modelSizeInfo(ttr::modelSize()).label]];
    const std::vector<int> selection = _kernel->instrumentSelection();
    if (!selection.empty())
        [parts addObject:[NSString stringWithFormat:@"%s %s", _kernel->modelFocus() ? "listening for" : "showing", ttr::selectionSummary(selection).c_str()]];
    int lo = 127, hi = 0;
    for (const ttr::NoteEvent& n : _doc->notes)
        if (n.program != ttr::kDrumProgram) { lo = std::min(lo, n.pitch); hi = std::max(hi, n.pitch); }
    if (lo <= hi) [parts addObject:[NSString stringWithFormat:@"%@ - %@", TTRNoteName(lo), TTRNoteName(hi)]];
    [parts addObject:[NSString stringWithFormat:@"%.2f s", _doc->lengthSec]];
    return [parts componentsJoinedByString:@"   ·   "];
}

- (void)drawStatusBar {
    const NSRect r = [self statusRect];
    [TTRHex(0x1A1C20) setFill];
    NSRectFill(r);
    [TTR_BORDER setFill];
    NSRectFill(NSMakeRect(r.origin.x, r.origin.y, r.size.width, 1));
    const ttr::Transcription t = _doc ? _doc->transcription : ttr::Transcription{};
    CGFloat right = NSMaxX(r) - 220;  // the zoom slider sits at the right end
    if (t.isActive()) {
        const NSRect bar = NSMakeRect(right - 180, NSMidY(r) - 3, 160, 6);
        TTRFillRounded(bar, 3, TTRHex(0x2B2E33));
        TTRFillRounded(NSMakeRect(bar.origin.x, bar.origin.y, bar.size.width * CGFloat(std::clamp(t.progress, 0.0f, 1.0f)), bar.size.height), 3, TTRHex(0x5B8CFF));
        NSString* label = t.phase == ttr::TranscriptionPhase::Loading ? @"Loading" : [NSString stringWithFormat:@"Transcribing %d %%", int(t.progress * 100.0f)];
        if (!t.message.empty()) label = @"Waiting for the model";
        TTRDrawText(label, NSMakeRect(bar.origin.x - 190, r.origin.y, 180, r.size.height), TTRFont(12, NSFontWeightSemibold), TTRHex(0x9DB8FF), NSTextAlignmentRight);
        right = bar.origin.x - 200;
    } else if (t.phase == ttr::TranscriptionPhase::Failed) {
        NSString* why = [NSString stringWithFormat:@"Transcription unavailable: %s", t.message.c_str()];
        TTRDrawText(why, NSMakeRect(right - 420, r.origin.y, 400, r.size.height), TTRFont(12, NSFontWeightSemibold), TTRHex(0xFFB35C), NSTextAlignmentRight);
        right -= 420;
    } else if (t.phase == ttr::TranscriptionPhase::Cancelled) {
        TTRDrawText(@"Transcription stopped - the notes so far are kept", NSMakeRect(right - 360, r.origin.y, 340, r.size.height), TTRFont(12), TTR_DIM,
                    NSTextAlignmentRight);
        right -= 360;
    }
    TTRDrawText([self statusText], NSMakeRect(r.origin.x + 24, r.origin.y, MAX(0.0, right - r.origin.x - 24), r.size.height), TTRMono(12, NSFontWeightRegular),
                TTR_DIM, NSTextAlignmentLeft, 0.3);
}

- (void)drawRect:(NSRect)dirty {
    const NSRect b = self.bounds;
    [TTR_BG setFill];
    NSRectFill(b);
    [self drawTopBar:b];
    [self drawSidebar:b];
    [self drawSongHeader];
    if (!_doc && !_kernel->isRecording()) [self drawDropZone];
    else if (_kernel->isRecording()) [self drawRecordingHint];
    [self drawStatusBar];
    ++_lastDrawCount;
}

@end
