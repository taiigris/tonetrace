// What the controls do: open / drop a song, record, transport, MIDI export, zoom and scroll, the instrument strips.
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "core/Instruments.h"
#include "core/Logger.h"
#include "core/SongImport.h"
#import "ui/TTRMainViewInternal.h"
#import "ui/TTRTheme.h"

static const double kWheelZoomStep = 0.01, kRowZoomStep = 0.005;

@implementation TTRMainView_v1 (Actions)

- (void)setParam:(int)paramId value:(float)value {
    _bridge->beginEdit(paramId);
    _bridge->setParameter(paramId, value);
    _bridge->endEdit(paramId);
}

// ------------------------------------------------------------ song and recording

- (void)openSongPanel {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.canChooseFiles = YES;
    panel.canChooseDirectories = NO;
    panel.allowsMultipleSelection = NO;
    panel.message = @"Choose a song - every instrument is transcribed, drums included";
    if (@available(macOS 11.0, *)) {
        NSMutableArray<UTType*>* types = [NSMutableArray array];
        for (const std::string& ext : ttr::songFileExtensions())
            if (UTType* type = [UTType typeWithFilenameExtension:[NSString stringWithUTF8String:ext.c_str()]]) [types addObject:type];
        panel.allowedContentTypes = types;
    }
    __weak TTRMainView_v1* weakSelf = self;
    void (^finish)(NSModalResponse) = ^(NSModalResponse response) {
        if (response == NSModalResponseOK && panel.URL.path) [weakSelf importSongAtPath:panel.URL.path];
    };
    if (self.window) [panel beginSheetModalForWindow:self.window completionHandler:finish];
    else finish([panel runModal]);
}

- (void)importSongAtPath:(NSString*)path {
    if (_kernel->isRecording()) {
        [self showMessage:@"Stop the recording first"];
        return;
    }
    if (!_kernel->loadSong(path.UTF8String)) {
        [self showMessage:@"Not a song file - use wav, aiff, mp3, m4a, flac or caf"];
        return;
    }
    [self showMessage:[NSString stringWithFormat:@"Loading %@ - you can play it as soon as the waveform appears", path.lastPathComponent]];
    [self refreshNow];
}

- (void)toggleRecord {
    const BOOL recording = _kernel->param(ttr::kParamRecord) >= 0.5f;
    if (!recording) _kernel->pause();
    [self setParam:ttr::kParamRecord value:recording ? 0.0f : 1.0f];
    [self showMessage:recording ? @"Recording stopped - transcribing it now" : @"Recording - play the track in your DAW, press ● again to stop"];
    [self refreshNow];
}

- (void)togglePlay {
    if (!_kernel->document()) {
        [self showMessage:@"Load a song first"];
        return;
    }
    _kernel->togglePlay();
    [self refreshNow];
}

- (void)rewind {
    _kernel->seek(0.0);
    if (!_tl->isFit) _tl->scrollSec = 0.0;
    [self refreshNow];
}

- (void)clearSong {
    _kernel->clearDocument();
    [self showMessage:@"Song removed"];
    [self refreshNow];
}

- (void)seekTo:(double)sec {
    _kernel->seek(sec);
    [self refreshNow];
}

// ------------------------------------------------------------ MIDI export

- (void)reportExportError:(NSString*)text {
    _kernel->diagnostics().setLastError(text.UTF8String);
    ttr::logError(_kernel->instanceId(), "MIDI export: %s", text.UTF8String);
    [self showBanner:text level:ttr::NoticeLevel::Error];
}

- (NSString*)exportSummary {
    const ttr::DocumentPtr doc = _kernel->document();
    if (!doc) return @"";
    const ttr::MixerState mix = _kernel->mixer();
    int tracks = 0, notes = 0;
    for (const ttr::InstrumentSummary& s : ttr::instrumentsIn(doc->notes))
        if (mix.isAudible(s.program)) { ++tracks; notes += s.noteCount; }
    return [NSString stringWithFormat:@"%d notes in %d instrument track%s", notes, tracks, tracks == 1 ? "" : "s"];
}

// Same name as an existing file gets " 2", " 3", ... so a file the DAW still holds open is never overwritten.
static NSURL* uniqueFileURL(NSURL* dir, NSString* fileName) {
    NSString* base = fileName.stringByDeletingPathExtension;
    NSURL* url = [dir URLByAppendingPathComponent:fileName];
    for (int n = 2; [NSFileManager.defaultManager fileExistsAtPath:url.path] && n < 1000; ++n)
        url = [dir URLByAppendingPathComponent:[NSString stringWithFormat:@"%@ %d.mid", base, n]];
    return url;
}

- (NSString*)midiFileName {
    return [[NSString stringWithUTF8String:_kernel->midiFileName().c_str()] stringByReplacingOccurrencesOfString:@"/" withString:@"-"];
}

- (NSURL*)writeMidiToMusicFolder {
    const std::vector<uint8_t> bytes = _kernel->buildMidi();
    const ttr::DocumentPtr doc = _kernel->document();
    if (!doc || doc->notes.empty() || bytes.empty()) {
        [self showMessage:@"No notes to export yet"];
        return nil;
    }
    NSURL* music = [NSFileManager.defaultManager URLsForDirectory:NSMusicDirectory inDomains:NSUserDomainMask].firstObject;
    NSURL* dir = [music URLByAppendingPathComponent:@"Tonetrace" isDirectory:YES];
    NSError* error = nil;
    if (!dir || ![NSFileManager.defaultManager createDirectoryAtURL:dir withIntermediateDirectories:YES attributes:nil error:&error]) {
        [self reportExportError:[NSString stringWithFormat:@"The MIDI file could not be written: cannot create ~/Music/Tonetrace (%@)",
                                                           error.localizedDescription ?: @"unknown error"]];
        return nil;
    }
    NSURL* url = uniqueFileURL(dir, [self midiFileName]);
    if (![[NSData dataWithBytes:bytes.data() length:bytes.size()] writeToURL:url options:NSDataWritingAtomic error:&error]) {
        [self reportExportError:[NSString stringWithFormat:@"The MIDI file could not be written: %@", error.localizedDescription]];
        return nil;
    }
    _kernel->diagnostics().setLastMidiPath(url.path.UTF8String);
    ttr::logInfo(_kernel->instanceId(), "MIDI export: %s", url.path.UTF8String);
    [self showMessage:[NSString stringWithFormat:@"%@ - drop it into the arrangement for one MIDI track per instrument", [self exportSummary]]];
    return url;
}

- (void)exportMidiWithPanel {
    const ttr::DocumentPtr doc = _kernel->document();
    if (!doc || doc->notes.empty()) { [self showMessage:@"No notes to export yet"]; return; }
    NSSavePanel* panel = [NSSavePanel savePanel];
    if (@available(macOS 11.0, *)) {
        if (UTType* midi = [UTType typeWithFilenameExtension:@"mid"]) panel.allowedContentTypes = @[ midi ];
    }
    panel.nameFieldStringValue = [self midiFileName];
    const std::vector<uint8_t> bytes = _kernel->buildMidi();
    NSData* data = [NSData dataWithBytes:bytes.data() length:bytes.size()];
    NSString* summary = [self exportSummary];
    __weak TTRMainView_v1* weakSelf = self;
    auto kernel = _kernel;
    void (^finish)(NSModalResponse) = ^(NSModalResponse response) {
        if (response != NSModalResponseOK || !panel.URL) return;
        NSError* error = nil;
        if ([data writeToURL:panel.URL options:NSDataWritingAtomic error:&error]) {
            kernel->diagnostics().setLastMidiPath(panel.URL.path.UTF8String);
            ttr::logInfo(kernel->instanceId(), "MIDI saved: %s", panel.URL.path.UTF8String);
            [weakSelf showMessage:[NSString stringWithFormat:@"Saved %@ (%@)", panel.URL.lastPathComponent, summary]];
        } else {
            [weakSelf reportExportError:[NSString stringWithFormat:@"The MIDI file could not be saved: %@", error.localizedDescription]];
        }
    };
    if (self.window) [panel beginSheetModalForWindow:self.window completionHandler:finish];
    else finish([panel runModal]);
}

// ------------------------------------------------------------ zoom and scroll

- (CGFloat)noteAreaWidth { return _roll.bounds.size.width - kTTRKeyboardWidth; }

// Zooms time so that the moment under `x` (note-area pixels; < 0: the centre) stays where it is.
- (void)zoomTo:(double)pxPerSec aroundX:(CGFloat)x {
    const CGFloat width = [self noteAreaWidth];
    if (width <= 0) return;
    const double fitPx = _tl->contentSec > 0.0 ? width / _tl->contentSec : ttr::kMinPixelsPerSecond;
    const double target = std::clamp(pxPerSec, std::min(fitPx, ttr::kMinPixelsPerSecond), ttr::kMaxPixelsPerSecond);
    const CGFloat anchor = x < 0 ? width / 2 : x;
    const double anchorSec = _tl->timeForX(anchor);
    _tl->pxPerSec = target;
    _tl->scrollSec = anchorSec - anchor / target;
    _tl->isFit = target <= fitPx * 1.0001;
    [self refreshNow];
}

- (void)handleWheel:(NSEvent*)event noteAreaX:(CGFloat)x {
    const NSEventModifierFlags flags = event.modifierFlags;
    const double dy = event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1.0 : 10.0);
    const double dx = event.scrollingDeltaX * (event.hasPreciseScrollingDeltas ? 1.0 : 10.0);
    if (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) {
        [self zoomTo:_tl->pxPerSec * std::exp(dy * kWheelZoomStep) aroundX:x];
        return;
    }
    if (flags & NSEventModifierFlagOption) {
        _tl->rowH = std::clamp(_tl->rowH * std::exp(dy * kRowZoomStep), ttr::kMinRowHeight, ttr::kMaxRowHeight);
        [self refreshNow];
        return;
    }
    const double timeDelta = (flags & NSEventModifierFlagShift) ? dy : dx;
    if (timeDelta != 0.0) {
        _tl->isFit = false;
        _tl->scrollSec -= timeDelta / _tl->pxPerSec;
    }
    if (!(flags & NSEventModifierFlagShift)) _tl->scrollY -= dy;
    [self refreshNow];
}

- (void)handleMagnify:(NSEvent*)event noteAreaX:(CGFloat)x { [self zoomTo:_tl->pxPerSec * (1.0 + event.magnification) aroundX:x]; }

// The sidebar scrolls its strips with the wheel.
- (void)scrollWheel:(NSEvent*)event {
    const NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
    if (p.x >= kTTRSidebarWidth) { [super scrollWheel:event]; return; }
    _stripScroll -= event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1.0 : 10.0);
    [self layoutControls];
}

// ------------------------------------------------------------ instrument strips

- (void)rebuildStripsIfNeeded {
    std::set<int> wanted;
    if (_doc)
        for (const ttr::InstrumentSummary& s : ttr::instrumentsIn(_doc->notes)) wanted.insert(s.program);
    std::set<int> shown;
    for (const auto& entry : _stripByProgram) shown.insert(entry.first);
    if (wanted == shown) return;
    __weak TTRMainView_v1* weak = self;
    auto kernel = _kernel;
    for (auto it = _stripByProgram.begin(); it != _stripByProgram.end();) {
        if (wanted.count(it->first)) { ++it; continue; }
        [it->second removeFromSuperview];
        it = _stripByProgram.erase(it);
    }
    for (int program : wanted) {
        if (_stripByProgram.count(program)) continue;
        TTRInstrumentStrip_v1* strip = [[TTRInstrumentStrip_v1 alloc] initWithProgram:program];
        strip.onMixChange = ^(int p, ttr::InstrumentMix mix) {
            kernel->setInstrumentMix(p, mix);
            [weak refreshNow];
        };
        [_strips addSubview:strip];
        _stripByProgram[program] = strip;
    }
    [self layoutControls];
}

- (void)updateStrips {
    if (!_doc || _stripByProgram.empty()) return;
    const ttr::MixerState mix = _kernel->mixer();
    for (const ttr::InstrumentSummary& s : ttr::instrumentsIn(_doc->notes)) {
        const auto it = _stripByProgram.find(s.program);
        if (it == _stripByProgram.end()) continue;
        TTRInstrumentStrip_v1* strip = it->second;
        int lo = 0, hi = 0;
        if (s.program == ttr::kDrumProgram) strip.info = [NSString stringWithFormat:@"%d hits · kit map", s.noteCount];
        else if (ttr::pitchRangeOf(_doc->notes, s.program, &lo, &hi))
            strip.info = [NSString stringWithFormat:@"%d notes · %@-%@", s.noteCount, TTRNoteName(lo), TTRNoteName(hi)];
        [strip setMix:mix.get(s.program)];
        strip.isAudible = mix.isAudible(s.program);
        [strip setLevel:_kernel->instrumentLevel(s.program)];
    }
}

@end
