// Note map: geometry, data from the main view, wheel / pinch navigation and keyboard commands.
#import "ui/TTRNoteMapView.h"

#include <algorithm>

#import "core/NoteEdit.h"
#import "ui/TTRNoteMapInternal.h"
#import "ui/TTRTheme.h"
#import "ui/TTRViewportInput.h"

static const CGFloat kNoteInsetPoints = 4.0;
static const NSInteger kKeyEscape = 53, kKeyBackspace = 51, kKeyForwardDelete = 117, kKeySpace = 49;

@implementation TTRNoteMapView_v1

- (instancetype)initWithKernel:(std::shared_ptr<ttr::TonetraceKernel>)kernel viewport:(std::shared_ptr<TTRViewport>)viewport {
    self = [super initWithFrame:NSMakeRect(0, 0, 700, 400)];
    if (self) {
        _kernel = std::move(kernel);
        _vp = std::move(viewport);
        _status = ttr::KernelStatus::Idle;
        _grabIndex = -1;
        _flashTrack = -1;
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawOnSetNeedsDisplay;
    }
    return self;
}

- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event { return YES; }
- (BOOL)isOpaque { return YES; }

// ------------------------------------------------------------ geometry

// The kernel's current notes (always up to date, even between two UI ticks).
- (const std::vector<ttr::NoteEvent>&)notes {
    static const std::vector<ttr::NoteEvent> none;
    _notesSnap = _kernel->snapshot();
    return _notesSnap ? _notesSnap->notes : none;
}

- (NSRect)rectForNote:(const ttr::NoteEvent&)n {
    const CGFloat x0 = _vp->xForTime(n.startSec), x1 = _vp->xForTime(n.endSec);
    const CGFloat height = MAX(6.0, _vp->rowH - kNoteInsetPoints);  // narrower than the row, exactly centred on it
    return NSMakeRect(x0, _vp->yTopForPitch(n.pitch) + (_vp->rowH - height) / 2, MAX(2.0, x1 - x0), height);
}

- (NSPoint)pointForTime:(double)seconds midi:(double)midi {
    return NSMakePoint(_vp->xForTime(seconds), [self yForMidi:midi]);
}

- (NSPoint)pointForTime:(double)seconds pitch:(int)pitch {
    return NSMakePoint(_vp->xForTime(seconds), _vp->yTopForPitch(pitch) + _vp->rowH / 2);
}

- (const ttr::PitchLines*)lines { return _mv.lines; }
- (double)tuningCents { return _kernel->tuningOffsetCents(); }
- (CGFloat)yForMidi:(double)midi { return (ttr::kMaxNote + 0.5 - (midi - [self tuningCents] / 100.0)) * _vp->rowH - _vp->scrollY; }

// The finished line under a view point (within kHoverSemitones of its pitch), if any.
- (ttr::LineHit)lineAtPoint:(NSPoint)p {
    const ttr::PitchLines* lines = [self lines];
    if (!lines || _status != ttr::KernelStatus::Idle) return {};
    return lines->hitAt(_vp->timeForX(p.x), _vp->pitchForY(p.y) + [self tuningCents] / 100.0, 0.6);
}

- (void)flashTrack:(int)track {
    _flashTrack = track;
    _flashStart = CACurrentMediaTime();
    for (int i = 1; i <= 8; ++i)
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, int64_t(i * 0.1 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ self.needsDisplay = YES; });
}

- (int)pitchAtY:(CGFloat)y {
    return std::clamp(int(std::lround(_vp->pitchForY(y))), ttr::kMinNote, ttr::kMaxNote);
}

- (NSRect)lassoRect {
    return NSMakeRect(MIN(_downPoint.x, _lassoEnd.x), MIN(_downPoint.y, _lassoEnd.y), fabs(_lassoEnd.x - _downPoint.x), fabs(_lassoEnd.y - _downPoint.y));
}

// Magnet: the row of the strongest line within one semitone of `pitch` between t0 and t1 (pitch lines; heat map energies without them).
- (int)magnetPitch:(int)pitch from:(double)t0 to:(double)t1 {
    if (const ttr::PitchLines* lines = [self lines]) return lines->magnetPitch(t0, t1, pitch, [self tuningCents]);
    return ttr::magnetPitch([self magnetEnergies], _mv.frames, _mv.hopSec, t0, t1, pitch);
}

// Energies the magnet looks at: what the map shows (harmonic part with Hide drums).
- (const float*)magnetEnergies {
    return _kernel->param(ttr::kParamHideDrums) >= 0.5f && _mv.harmonic ? _mv.harmonic : _mv.raw;
}

- (void)notifyViewport {
    TTRClampViewport(_vp.get(), self.bounds.size);
    self.needsDisplay = YES;
    if (_onViewportChange) _onViewportChange();
}

- (void)sendMessage:(NSString*)message { if (_onMessage) _onMessage(message); }

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    TTRClampViewport(_vp.get(), size);
}

- (void)resetCursorRects { [self addCursorRect:self.bounds cursor:[NSCursor crosshairCursor]]; }

- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea* a in self.trackingAreas) [self removeTrackingArea:a];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds
                                                       options:NSTrackingMouseMoved | NSTrackingMouseEnteredAndExited | NSTrackingActiveInActiveApp | NSTrackingInVisibleRect
                                                         owner:self userInfo:nil]];
}

- (void)drawRect:(NSRect)dirty { [self drawContent]; }

// ------------------------------------------------------------ data from the main view

// Everything that changes the picture; the view only redraws when this string changes.
- (NSString*)stateKey {
    const TTRViewport& v = *_vp;
    return [NSString stringWithFormat:@"%p|%d|%d|%d|%.1f|%d|%.4f|%.4f|%.2f|%.2f|%.0f|%.0f|%.3f|%d|%.3f|%.2f|%.2f|%d", _mv.snapshot.get(), _mv.frames, _mv.lineFrames, int(_status),
            [self tuningCents], _kernel->lineView().fundamentals ? 1 : 0,
            v.scrollSec, v.pxPerSec, v.scrollY, v.rowH, self.bounds.size.width, self.bounds.size.height, _recorded, _kernel->previewing() ? 1 : 0, _playhead,
            double(_kernel->param(ttr::kParamGain)), double(_kernel->param(ttr::kParamRange)), _kernel->param(ttr::kParamHideDrums) >= 0.5f ? 1 : 0];
}

- (void)updateWithMapView:(const ttr::MapView&)view status:(ttr::KernelStatus)status recorded:(double)recorded playhead:(double)playhead {
    _mv = view;
    _status = status;
    _recorded = recorded;
    _playhead = playhead;
    _grid = _kernel->timeGrid();
    NSString* key = [self stateKey];
    if (![key isEqualToString:_drawKey]) {
        _drawKey = key;
        self.needsDisplay = YES;
    }
}

// ------------------------------------------------------------ navigation

- (NSPoint)anchorForEvent:(NSEvent*)event { return [self convertPoint:event.locationInWindow fromView:nil]; }

- (void)scrollWheel:(NSEvent*)event {
    __weak TTRNoteMapView_v1* weak = self;
    TTRHandleWheel(event, _vp.get(), [self anchorForEvent:event], self.bounds.size, YES, ^(double speed) { if (weak.onZoom) weak.onZoom(speed); });
    [self notifyViewport];
}

- (void)magnifyWithEvent:(NSEvent*)event {
    __weak TTRNoteMapView_v1* weak = self;
    TTRHandlePinch(event, _vp.get(), [self anchorForEvent:event], self.bounds.size, ^(double speed) { if (weak.onZoom) weak.onZoom(speed); });
    [self notifyViewport];
}

// ------------------------------------------------------------ edit commands

- (void)undoEdit { if (!_kernel->undo()) [self sendMessage:@"Nothing to undo"]; }
- (void)redoEdit { if (!_kernel->redo()) [self sendMessage:@"Nothing to redo"]; }

- (void)deleteSelectedNotes {
    if (ttr::selectedCount([self notes]) == 0) { [self sendMessage:@"Select a note first (click it), then press Backspace"]; return; }
    _kernel->editNotes(ttr::withSelectedDeleted([self notes]));
}

- (void)clearAllNotes {
    if ([self notes].empty()) { [self sendMessage:@"There are no notes to clear"]; return; }
    _kernel->editNotes({});  // one undo step
}

- (void)selectAllNotes { _kernel->setNotes(ttr::withAllSelected([self notes], true)); }

- (void)duplicateSelectedNotes {
    if (ttr::selectedCount([self notes]) == 0) { [self sendMessage:@"Select notes first, then duplicate them with Cmd+D"]; return; }
    _kernel->editNotes(ttr::withDuplicated([self notes]));
}

- (BOOL)performKeyEquivalent:(NSEvent*)event {
    if (!(event.modifierFlags & NSEventModifierFlagCommand) || self.window.firstResponder != self) return NO;
    NSString* key = event.charactersIgnoringModifiers.lowercaseString;
    if ([key isEqualToString:@"z"]) { (event.modifierFlags & NSEventModifierFlagShift) ? [self redoEdit] : [self undoEdit]; return YES; }
    if ([key isEqualToString:@"a"]) { [self selectAllNotes]; return YES; }
    if ([key isEqualToString:@"d"]) { [self duplicateSelectedNotes]; return YES; }
    return NO;
}

- (void)keyDown:(NSEvent*)event {
    switch (event.keyCode) {
        case kKeyBackspace:
        case kKeyForwardDelete: [self deleteSelectedNotes]; break;
        case kKeyEscape: _kernel->setNotes(ttr::withAllSelected([self notes], false)); break;
        case kKeySpace: if (_onTogglePreview) _onTogglePreview(); break;
        default: [super keyDown:event];
    }
}

@end
