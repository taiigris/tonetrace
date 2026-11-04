// The song's waveform above the piano roll (NeuralNote-style): amplitude axis on the left, bars, playhead with a
// triangle, the time ruler below. A click or drag moves the playhead.
#pragma once
#import <Cocoa/Cocoa.h>

#include <memory>

#include "core/TonetraceKernel.h"
#import "ui/TTRTimeline.h"

constexpr CGFloat kTTRKeyboardWidth = 80.0;  // left gutter shared with the piano roll's keyboard
constexpr CGFloat kTTRRulerHeight = 28.0;

@interface TTRWaveLane_v1 : NSView
@property(nonatomic, copy) void (^onSeek)(double sec);
@property(nonatomic, copy) void (^onWheel)(NSEvent* event, CGFloat noteAreaX);
- (instancetype)initWithTimeline:(std::shared_ptr<TTRTimeline>)timeline;
// Everything one frame draws: the document (may be null), the live recording, the playhead.
- (void)updateWithDocument:(ttr::DocumentPtr)doc recording:(const ttr::RecordingView&)recording isRecording:(BOOL)isRecording playhead:(double)playhead;
@end
