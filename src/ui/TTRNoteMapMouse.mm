// Note map: drawing, selecting, moving, resizing and deleting notes with the mouse.
#import "core/NoteEdit.h"
#import "ui/TTRNoteMapInternal.h"
#import "ui/TTRTheme.h"

const CGFloat kTTREdgeGrabPixels = 6.0;
static const CGFloat kDragStartPixels = 3.0, kHitPadding = 1.0;
static const double kAuditionProbeSec = 0.1;

typedef struct { int index; TTRGesture gesture; } TTRNoteHit;

@implementation TTRNoteMapView_v1 (Mouse)

- (NSPoint)pointOfEvent:(NSEvent*)event { return [self convertPoint:event.locationInWindow fromView:nil]; }
- (BOOL)snapEnabled:(NSEvent*)event { return !(event.modifierFlags & NSEventModifierFlagOption); }  // Alt = free

// Top-most note under the point, and what a drag from there does (resize at the ends, move elsewhere).
- (TTRNoteHit)hitNoteAt:(NSPoint)p {
    const std::vector<ttr::NoteEvent>& notes = [self notes];
    for (int i = int(notes.size()) - 1; i >= 0; --i) {
        const NSRect r = NSInsetRect([self rectForNote:notes[size_t(i)]], -kHitPadding, -kHitPadding);
        if (!NSPointInRect(p, r)) continue;
        const CGFloat edge = MIN(kTTREdgeGrabPixels, r.size.width / 3.0);
        if (p.x <= NSMinX(r) + edge) return {i, TTRGestureResizeStart};
        if (p.x >= NSMaxX(r) - edge) return {i, TTRGestureResizeEnd};
        return {i, TTRGestureMove};
    }
    return {-1, TTRGestureNone};
}

- (const ttr::NoteEvent&)noteAt:(int)index { return [self notes][size_t(index)]; }

- (void)updateCursorFor:(NSPoint)p {
    _vp->hasCursor = NSPointInRect(p, self.bounds);
    _vp->cursorSec = _vp->timeForX(p.x);
    _vp->cursorPitch = _vp->pitchForY(p.y);
    const TTRNoteHit hit = _gesture == TTRGestureNone ? [self hitNoteAt:p] : TTRNoteHit{-1, _gesture};
    const TTRGesture zone = _gesture == TTRGestureNone ? hit.gesture : _gesture;
    if (zone == TTRGestureResizeStart || zone == TTRGestureResizeEnd) [[NSCursor resizeLeftRightCursor] set];
    else if (zone == TTRGestureMove) [[NSCursor openHandCursor] set];
    else [[NSCursor crosshairCursor] set];
    self.needsDisplay = YES;
    [self notifyViewport];
}

- (void)mouseMoved:(NSEvent*)event { [self updateCursorFor:[self pointOfEvent:event]]; }
- (void)mouseEntered:(NSEvent*)event { [self updateCursorFor:[self pointOfEvent:event]]; }
- (void)mouseExited:(NSEvent*)event { _vp->hasCursor = NO; self.needsDisplay = YES; [self notifyViewport]; }

- (void)beginGesture:(TTRGesture)gesture at:(NSPoint)p {
    _gesture = gesture;
    _downPoint = _lassoEnd = p;
    _downSec = _vp->timeForX(p.x);
    _downPitch = [self pitchAtY:p.y];
    _gestureChanged = _gestureStarted = NO;
    _grid = _kernel->timeGrid();
    const ttr::SnapshotPtr snap = _kernel->snapshot();
    _origNotes = snap ? snap->notes : std::vector<ttr::NoteEvent>{};
}

- (void)commitLive:(std::vector<ttr::NoteEvent>)notes {
    if (!_gestureChanged) { _kernel->checkpoint(); _gestureChanged = YES; }  // one undo step per gesture
    _kernel->setNotes(std::move(notes));
}

// Right-click / Ctrl-click: context menu on a note or on the map. A note under the pointer becomes the selection.
- (NSMenu*)menuForEvent:(NSEvent*)event {
    const TTRNoteHit hit = [self hitNoteAt:[self pointOfEvent:event]];
    if (hit.index >= 0 && ![self noteAt:hit.index].selected) _kernel->setNotes(ttr::withSelection([self notes], {hit.index}, ttr::SelectMode::Replace));
    const BOOL hasSelection = ttr::selectedCount([self notes]) > 0, hasNotes = ![self notes].empty();
    NSMenu* menu = [[NSMenu alloc] initWithTitle:@"Notes"];
    menu.autoenablesItems = NO;
    struct Entry { NSString* title; SEL action; BOOL enabled; };
    const Entry entries[] = {{@"Delete", @selector(deleteSelectedNotes), hasSelection}, {@"Duplicate", @selector(duplicateSelectedNotes), hasSelection},
                             {nil, nullptr, YES}, {@"Select all", @selector(selectAllNotes), hasNotes}, {@"Clear notes", @selector(clearAllNotes), hasNotes}};
    for (const Entry& e : entries) {
        if (!e.title) { [menu addItem:NSMenuItem.separatorItem]; continue; }
        NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:e.title action:e.action keyEquivalent:@""];
        item.target = self;
        item.enabled = e.enabled;
        [menu addItem:item];
    }
    return menu;
}

- (void)mouseDown:(NSEvent*)event {
    [self handleMouseDown:event];
    self.needsDisplay = YES;
}

- (void)handleMouseDown:(NSEvent*)event {
    [self.window makeFirstResponder:self];
    const NSPoint p = [self pointOfEvent:event];
    if (event.modifierFlags & NSEventModifierFlagControl) { [NSMenu popUpContextMenu:[self menuForEvent:event] withEvent:event forView:self]; return; }
    const BOOL shift = (event.modifierFlags & NSEventModifierFlagShift) != 0;
    if (event.modifierFlags & NSEventModifierFlagCommand) { [self beginGesture:TTRGestureLasso at:p]; return; }

    const TTRNoteHit hit = [self hitNoteAt:p];
    if (hit.index >= 0) {
        if (event.clickCount >= 2) { _kernel->editNotes(ttr::withDeleted([self notes], {hit.index})); return; }
        if (shift) { _kernel->setNotes(ttr::withSelection([self notes], {hit.index}, ttr::SelectMode::Toggle)); return; }
        if (![self noteAt:hit.index].selected) _kernel->setNotes(ttr::withSelection([self notes], {hit.index}, ttr::SelectMode::Replace));
        [self beginGesture:hit.gesture at:p];
        _origIndices = ttr::selectedIndices(_origNotes);
        _grabIndex = hit.index;
        _grabStart = _origNotes[size_t(hit.index)].startSec;
        return;
    }
    if (!shift && ttr::selectedCount([self notes]) > 0) _kernel->setNotes(ttr::withAllSelected([self notes], false));
    [self beginGesture:TTRGestureDraw at:p];
    _origNotes = [self notes];
    const double t = _downSec;
    int pitch = _downPitch;
    if (_kernel->param(ttr::kParamMagnet) >= 0.5f) pitch = ttr::magnetPitch([self magnetEnergies], _mv.frames, _mv.hopSec, t, t + kAuditionProbeSec, pitch);
    _kernel->audition(pitch);  // hear the row you are about to draw on
}

- (void)dragDraw:(NSPoint)p snap:(BOOL)snap {
    const double t1 = _vp->timeForX(p.x);
    int pitch = _downPitch;
    if (_kernel->param(ttr::kParamMagnet) >= 0.5f)
        pitch = ttr::magnetPitch([self magnetEnergies], _mv.frames, _mv.hopSec, MIN(_downSec, t1), MAX(_downSec, t1), pitch);
    [self commitLive:ttr::withNoteAdded(_origNotes, ttr::makeNote(pitch, _downSec, t1, _grid, snap))];
}

- (void)dragMove:(NSPoint)p snap:(BOOL)snap {
    double newStart = _grabStart + (_vp->timeForX(p.x) - _downSec);
    if (snap) newStart = ttr::snapTime(newStart, _grid);
    const int dPitch = [self pitchAtY:p.y] - _downPitch;
    [self commitLive:ttr::withMoved(_origNotes, _origIndices, newStart - _grabStart, dPitch)];
}

- (void)dragResize:(NSPoint)p snap:(BOOL)snap {
    double t = _vp->timeForX(p.x);
    if (snap) t = ttr::snapTime(t, _grid);
    const double minLen = snap ? ttr::minNoteSec(_grid) : ttr::kMinFreeNoteSec;
    [self commitLive:ttr::withResized(_origNotes, _grabIndex, _gesture == TTRGestureResizeStart, t, minLen)];
}

- (void)dragLasso:(NSPoint)p additive:(BOOL)additive {
    _lassoEnd = p;
    const NSRect r = [self lassoRect];
    const double t0 = _vp->timeForX(NSMinX(r)), t1 = _vp->timeForX(NSMaxX(r));
    const int low = [self pitchAtY:NSMaxY(r)], high = [self pitchAtY:NSMinY(r)];
    const std::vector<int> hits = ttr::indicesInRegion(_origNotes, t0, t1, low, high);
    _kernel->setNotes(ttr::withSelection(_origNotes, hits, additive ? ttr::SelectMode::Add : ttr::SelectMode::Replace));
    self.needsDisplay = YES;
}

- (void)mouseDragged:(NSEvent*)event {
    if (_gesture == TTRGestureNone) return;
    const NSPoint p = [self pointOfEvent:event];
    _vp->hasCursor = YES;
    _vp->cursorSec = _vp->timeForX(p.x);
    _vp->cursorPitch = _vp->pitchForY(p.y);
    if (!_gestureStarted && hypot(p.x - _downPoint.x, p.y - _downPoint.y) < kDragStartPixels) return;
    _gestureStarted = YES;
    const BOOL snap = [self snapEnabled:event];
    switch (_gesture) {
        case TTRGestureDraw: [self dragDraw:p snap:snap]; break;
        case TTRGestureMove: [self dragMove:p snap:snap]; break;
        case TTRGestureResizeStart:
        case TTRGestureResizeEnd: [self dragResize:p snap:snap]; break;
        case TTRGestureLasso: [self dragLasso:p additive:(event.modifierFlags & NSEventModifierFlagShift) != 0]; break;
        default: break;
    }
    [self notifyViewport];
}

- (void)mouseUp:(NSEvent*)event {
    _gesture = TTRGestureNone;
    _gestureStarted = NO;
    [self updateCursorFor:[self pointOfEvent:event]];
}

@end
