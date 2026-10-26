// Tonetrace brand drawing with CoreGraphics (vector, resolution independent). The geometry is taken 1:1 from assets/logo/*.svg.
// All functions expect a context with y pointing down (flipped NSView or a flipped bitmap context).
#pragma once
#import <CoreGraphics/CoreGraphics.h>

// The mark: waveform bars turning into staff lines plus the orange accent note (512 x 512 units, fitted into rect).
void TTRDrawMark(CGContextRef ctx, CGRect rect);
// Mark plus the TONETRACE lettering (viewBox 1590 x 180 of tonetrace-wordmark.svg, fitted into rect, aspect ratio kept, left aligned).
void TTRDrawWordmark(CGContextRef ctx, CGRect rect);
// Only the TONETRACE lettering (1330 x 140 units, fitted into rect, centred).
void TTRDrawLettering(CGContextRef ctx, CGRect rect, CGColorRef color);
// The 1024 x 1024 app icon tile of tonetrace-icon.svg, fitted into rect.
void TTRDrawIconTile(CGContextRef ctx, CGRect rect);

extern const CGFloat kTTRWordmarkAspect;  // width / height of the wordmark
extern const CGFloat kTTRLetteringAspect;
