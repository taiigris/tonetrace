// One instrument of the sidebar (NeuralNote-style strip): coloured chip, name, "618 notes · E1-G4", Mute / Solo,
// fader with its dB value and a level meter.
#pragma once
#import <Cocoa/Cocoa.h>

#include "core/Mixer.h"

@interface TTRInstrumentStrip_v1 : NSView
@property(nonatomic, readonly) int program;
@property(nonatomic, copy) NSString* info;  // second line
@property(nonatomic) BOOL isAudible;         // false: dimmed (muted or another instrument is soloed)
@property(nonatomic, copy) void (^onMixChange)(int program, ttr::InstrumentMix mix);
- (instancetype)initWithProgram:(int)program;
- (void)setMix:(const ttr::InstrumentMix&)mix;  // ignored while the fader is dragged
- (void)setLevel:(float)level;
@end

constexpr CGFloat kTTRStripHeight = 112.0;
