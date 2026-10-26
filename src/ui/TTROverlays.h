// Overlays of the main window: the notice banner and the diagnostics panel.
#pragma once
#import <Cocoa/Cocoa.h>

#include "core/Diagnostics.h"

// One-line banner (info / warning / error) with a close button. Info and warnings fade after a while, errors stay.
@interface TTRBannerView_v1 : NSView
- (void)showText:(NSString*)text level:(ttr::NoticeLevel)level;
- (void)hideBanner;
@property(nonatomic, readonly) BOOL isShowing;
@property(nonatomic, readonly, copy) NSString* text;
@end

// Diagnostics overlay: version, host, load, overruns, worker lag, recording memory, UI frame time, last MIDI file.
@interface TTRDiagnosticsView_v1 : NSView
@property(nonatomic, copy) void (^onClose)(void);
@property(nonatomic, copy) void (^onMessage)(NSString* message);
- (void)updateWithSnapshot:(const ttr::DiagnosticsSnapshot&)snapshot;
- (NSString*)reportText;  // what "Copy report" puts on the clipboard
- (void)copyReport;
- (void)saveReport;
- (void)openLogFolder;
@end
