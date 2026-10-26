// Piano keyboard left of the note map; follows the vertical scroll and zoom of the shared viewport.
#pragma once
#import <Cocoa/Cocoa.h>

#include <memory>

#include "ui/TTRViewport.h"

@interface TTRPianoKeys_v1 : NSView
@property(nonatomic, copy) void (^onKeyClick)(int pitch);
@property(nonatomic, copy) void (^onScroll)(void);
- (instancetype)initWithViewport:(std::shared_ptr<TTRViewport>)viewport;
@end
