// Note map: spectrogram heatmap with semitone rows where the user draws, moves, resizes and deletes notes.
#pragma once
#import <Cocoa/Cocoa.h>

#include <memory>

#include "core/TonetraceKernel.h"
#include "ui/TTRViewport.h"

@interface TTRNoteMapView_v1 : NSView
@property(nonatomic, copy) void (^onZoom)(double speed);        // Cmd+wheel / pinch: new Speed parameter value
@property(nonatomic, copy) void (^onMessage)(NSString* message);
@property(nonatomic, copy) void (^onTogglePreview)(void);
@property(nonatomic, copy) void (^onViewportChange)(void);      // scrolled, zoomed or the pointer moved

- (instancetype)initWithKernel:(std::shared_ptr<ttr::TonetraceKernel>)kernel viewport:(std::shared_ptr<TTRViewport>)viewport;
- (void)updateWithMapView:(const ttr::MapView&)view status:(ttr::KernelStatus)status recorded:(double)recorded playhead:(double)playhead;
- (NSPoint)pointForTime:(double)seconds pitch:(int)pitch;  // centre of that row at that time, in view coordinates

// Edit commands (keyboard shortcuts and the main view's buttons)
- (void)undoEdit;
- (void)redoEdit;
- (void)deleteSelectedNotes;
- (void)selectAllNotes;
- (void)duplicateSelectedNotes;
- (void)clearAllNotes;
@end
