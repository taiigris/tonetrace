// Note map: geometry, data from the main view, wheel / pinch navigation and keyboard commands.
#import "ui/TTRNoteMapView.h"

#include <algorithm>

#import "core/NoteEdit.h"
#import "ui/TTRNoteMapInternal.h"
#import "ui/TTRTheme.h"
#import "ui/TTRViewportInput.h"

static const NSInteger kKeyEscape = 53, kKeyBackspace = 51, kKeyForwardDelete = 117, kKeySpace = 49;

@implementation TTRNoteMapView_v1

- (instancetype)initWithKernel:(std::shared_ptr<ttr::TonetraceKernel>)kernel viewport:(std::shared_ptr<TTRViewport>)viewport {
    self = [super initWithFrame:NSMakeRect(0, 0, 700, 400)];
    if (self) {
        _kernel = std::move(kernel);
        _vp = std::move(viewport);
        _status = ttr::KernelStatus::Idle;
        _grabIndex = -1;
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
    return NSMakeRect(x0, _vp->yTopForPitch(n.pitch) + 1, MAX(2.0, x1 - x0), _vp->rowH - 2);
}

- (NSPoint)pointForTime:(double)seconds pitch:(int)pitch {
    return NSMakePoint(_vp->xForTime(seconds), _vp->yTopForPitch(pitch) + _vp->rowH / 2);
}

- (int)pitchAtY:(CGFloat)y {
    return std::clamp(int(std::lround(_vp->pitchForY(y))), ttr::kMinNote, ttr::kMaxNote);
}

- (NSRect)lassoRect {
    return NSMakeRect(MIN(_downPoint.x, _lassoEnd.x), MIN(_downPoint.y, _lassoEnd.y), fabs(_lassoEnd.x - _downPoint.x), fabs(_lassoEnd.y - _downPoint.y));
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
    return [NSString stringWithFormat:@"%p|%d|%d|%.4f|%.4f|%.2f|%.2f|%.0f|%.0f|%.3f|%d|%.3f|%.2f|%.2f|%d", _mv.snapshot.get(), _mv.frames, int(_status),
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
