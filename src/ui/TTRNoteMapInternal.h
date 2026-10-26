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
- (void)notifyViewport;
- (void)sendMessage:(NSString*)message;
@end

@interface TTRNoteMapView_v1 (Drawing)
- (void)drawContent;
@end
