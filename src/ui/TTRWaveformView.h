// Ruler and waveform lane above the note map: same time window, preview start and loop region.
#pragma once
#import <Cocoa/Cocoa.h>

#include <memory>

#include "core/TonetraceKernel.h"
#include "ui/TTRViewport.h"

extern const CGFloat kTTRRulerHeight;

@interface TTRWaveformView_v1 : NSView
@property(nonatomic, copy) void (^onZoom)(double speed);
@property(nonatomic, copy) void (^onViewportChange)(void);

- (instancetype)initWithKernel:(std::shared_ptr<ttr::TonetraceKernel>)kernel viewport:(std::shared_ptr<TTRViewport>)viewport;
- (void)updateWithMapView:(const ttr::MapView&)view status:(ttr::KernelStatus)status recorded:(double)recorded playhead:(double)playhead;
- (NSPoint)pointForTime:(double)seconds;  // x = time, y = middle of the lane (view coordinates)
@end
