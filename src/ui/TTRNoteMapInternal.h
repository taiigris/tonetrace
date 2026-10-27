// Private state shared between the note map's implementation files.
#pragma once
#import "ui/TTRHeatTiles.h"
#import "ui/TTRNoteMapView.h"

typedef NS_ENUM(NSInteger, TTRGesture) {
    TTRGestureNone = 0,
    TTRGestureDraw,
    TTRGestureMove,
    TTRGestureResizeStart,
    TTRGestureResizeEnd,
    TTRGestureLasso,
};

extern const CGFloat kTTREdgeGrabPixels;  // width of the resize zones at both ends of a note

// Notes are drawn as flat bars about as thick as the core of a line, centred in their row (the row rect stays the hit area).
static const CGFloat kTTRBarMinHeight = 5.0, kTTRBarMaxHeight = 7.0;
static inline NSRect TTRNoteBarRect(NSRect row) {
    const CGFloat h = MIN(kTTRBarMaxHeight, MAX(kTTRBarMinHeight, row.size.height * 0.5));
    return NSMakeRect(row.origin.x, row.origin.y + (row.size.height - h) / 2, row.size.width, h);
}

@interface TTRNoteMapView_v1 () {
@public
    std::shared_ptr<ttr::TonetraceKernel> _kernel;
    std::shared_ptr<TTRViewport> _vp;
    ttr::MapView _mv;
    ttr::SnapshotPtr _notesSnap;  // keeps the list returned by -notes alive
    ttr::KernelStatus _status;
    double _recorded, _playhead;
    ttr::TimeGrid _grid;
    TTRHeatTileCache _tiles;
    NSString* _drawKey;
    int _flashTrack;               // line that lights up after click-to-grab (-1: none)
    CFTimeInterval _flashStart, _lastGrabTime;
    // gesture state
    TTRGesture _gesture;
    NSPoint _downPoint, _lassoEnd;
    double _downSec, _grabStart;
    int _downPitch, _grabIndex;
    BOOL _gestureChanged, _gestureStarted;
    std::vector<ttr::NoteEvent> _origNotes;
    std::vector<int> _origIndices;
}
- (const std::vector<ttr::NoteEvent>&)notes;
- (NSRect)rectForNote:(const ttr::NoteEvent&)note;
- (NSRect)lassoRect;
- (int)pitchAtY:(CGFloat)y;
- (const float*)magnetEnergies;
- (int)magnetPitch:(int)pitch from:(double)t0 to:(double)t1;
- (const ttr::PitchLines*)lines;   // finished pitch lines of the recording; null while recording or without lines
- (double)tuningCents;              // grid offset (detected tuning when aligned, plus the manual trim)
- (CGFloat)yForMidi:(double)midi;   // view y of a line pitch (tuning applied)
- (ttr::LineHit)lineAtPoint:(NSPoint)p;
- (void)flashTrack:(int)track;
- (void)notifyViewport;
- (void)sendMessage:(NSString*)message;
@end

@interface TTRNoteMapView_v1 (Mouse)
- (int)hitNoteIndexAt:(NSPoint)p;  // top-most drawn note under the point, -1 for none
@end

@interface TTRNoteMapView_v1 (Drawing)
- (void)drawContent;
@end

// Hover row, line outline, ghost note and the label at the pointer.
@interface TTRNoteMapView_v1 (Hover)
- (void)drawHover:(CGContextRef)ctx;
- (void)strokeTrack:(int)track context:(CGContextRef)ctx width:(CGFloat)width color:(NSColor*)color;
- (BOOL)hasHoverPoint;
- (NSPoint)cursorPoint;
- (ttr::LineHit)hoveredLine;
- (BOOL)ghostNote:(ttr::NoteEvent*)out;
- (void)drawGhostAndLabel:(CGContextRef)ctx;
@end
