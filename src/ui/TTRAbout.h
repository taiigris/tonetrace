// Brand views: the clickable logo in the panel header and the About overlay.
#pragma once
#import <Cocoa/Cocoa.h>

// The wordmark (mark + lettering) drawn natively; a click opens the About overlay.
@interface TTRLogoView_v1 : NSView
@property(nonatomic, copy) void (^onClick)(void);
@end

// About overlay: dimmed backdrop and a centred card with version, build, claim, copyright and two links.
// Closes on a click outside the card, Esc and the close button; fades in and out in 150 ms.
@interface TTRAboutView_v1 : NSView
@property(nonatomic, copy) void (^onOpenDiagnostics)(void);
@property(nonatomic, copy) void (^onOpenLogFolder)(void);
@property(nonatomic, readonly) BOOL isShowing;  // flips immediately, also while the fade is still running
- (void)show;
- (void)dismiss;
@end
