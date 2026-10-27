// Root view of the Tonetrace plugin window: header, waveform lane, note map, piano keys and the side panel.
#pragma once
#import <Cocoa/Cocoa.h>

#include <memory>

#include "ui/TTRHostBridge.h"

extern const NSSize kTTRDefaultSize;  // 1280 x 720
extern const NSSize kTTRMinimumSize;  // 1024 x 600

@interface TTRMainView_v1 : NSView
- (instancetype)initWithBridge:(std::shared_ptr<ttr::TTRHostBridge>)bridge;
- (void)refreshNow;    // one UI update tick (also used by the tools)
- (void)shutdown;      // stops the timer; the view stays drawable
@end
