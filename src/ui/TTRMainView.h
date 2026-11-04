// Root view of the Tonetrace plug-in window (NeuralNote-style): transport bar, instrument sidebar, song header,
// waveform lane, piano roll and status bar.
#pragma once
#import <Cocoa/Cocoa.h>

#include <memory>

#include "ui/TTRHostBridge.h"

extern const NSSize kTTRDefaultSize;  // 1280 x 800
extern const NSSize kTTRMinimumSize;  // 1024 x 640

@interface TTRMainView_v1 : NSView
- (instancetype)initWithBridge:(std::shared_ptr<ttr::TTRHostBridge>)bridge;
- (void)refreshNow;    // one UI update tick (also used by the tools)
- (void)shutdown;      // stops the timer; the view stays drawable
@end
