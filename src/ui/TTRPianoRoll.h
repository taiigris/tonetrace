// The piano roll (NeuralNote-style): keyboard on the left, every transcribed note as a bar in its instrument's colour
// (dimmed when the instrument is muted), the playhead, and while the model works the part of the song it has not
// reached yet. Notes are shown, not edited. A click moves the playhead; hovering a note names it.
#pragma once
#import <Cocoa/Cocoa.h>

#include <memory>

#include "core/Mixer.h"
#include "core/Types.h"
#import "ui/TTRTimeline.h"

@interface TTRPianoRoll_v1 : NSView
@property(nonatomic, copy) void (^onSeek)(double sec);
@property(nonatomic, copy) void (^onWheel)(NSEvent* event, CGFloat noteAreaX);
@property(nonatomic, copy) void (^onMagnify)(NSEvent* event, CGFloat noteAreaX);
- (instancetype)initWithTimeline:(std::shared_ptr<TTRTimeline>)timeline;
- (void)updateWithDocument:(ttr::DocumentPtr)doc mixer:(const ttr::MixerState&)mixer playhead:(double)playhead;
@end
