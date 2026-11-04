// Colours, fonts and small drawing helpers shared by all Tonetrace views.
#pragma once
#import <Cocoa/Cocoa.h>

static inline NSColor* TTRHex(uint32_t rgb, CGFloat alpha = 1.0) {
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0 green:((rgb >> 8) & 0xFF) / 255.0 blue:(rgb & 0xFF) / 255.0 alpha:alpha];
}

// One neutral grey palette and a single accent (turquoise); red only while recording, white marks a selection.
#define TTR_BG TTRHex(0x111214)
#define TTR_PANEL TTRHex(0x1A1C1F)
#define TTR_PANEL_RAISED TTRHex(0x212428)
#define TTR_HOVER TTRHex(0x2A2D31)
#define TTR_BORDER TTRHex(0x2A2D31)
#define TTR_ACCENT TTRHex(0x2EE6C5)
#define TTR_ACCENT_TINT TTRHex(0x2EE6C5, 0.16)
#define TTR_ON_ACCENT TTRHex(0x0A1411)
#define TTR_SELECT TTRHex(0xFFFFFF)
#define TTR_RECORD TTRHex(0xFF4D5E)
#define TTR_NOTE TTRHex(0x2EE6C5)
#define TTR_TEXT TTRHex(0xE7E9EC)
#define TTR_DIM TTRHex(0x8B9099)

constexpr CGFloat kTTRRadius = 6.0;  // every flat control

static inline NSFont* TTRFont(CGFloat size, NSFontWeight weight = NSFontWeightRegular) {
    return [NSFont systemFontOfSize:size weight:weight];
}
static inline NSFont* TTRMono(CGFloat size, NSFontWeight weight = NSFontWeightMedium) {
    return [NSFont monospacedDigitSystemFontOfSize:size weight:weight];
}

static inline NSDictionary* TTRTextAttrs(NSFont* font, NSColor* color, CGFloat kern = 0.0) {
    return @{NSFontAttributeName : font, NSForegroundColorAttributeName : color, NSKernAttributeName : @(kern)};
}

// Draws single-line text inside `rect`, vertically centred. align: NSTextAlignmentLeft/Center/Right.
static inline void TTRDrawText(NSString* text, NSRect rect, NSFont* font, NSColor* color, NSTextAlignment align = NSTextAlignmentLeft,
                               CGFloat kern = 0.0) {
    NSMutableParagraphStyle* style = [[NSMutableParagraphStyle alloc] init];
    style.alignment = align;
    style.lineBreakMode = NSLineBreakByClipping;
    NSMutableDictionary* attrs = [TTRTextAttrs(font, color, kern) mutableCopy];
    attrs[NSParagraphStyleAttributeName] = style;
    const CGFloat h = ceil(font.ascender - font.descender);
    NSRect r = NSMakeRect(rect.origin.x, rect.origin.y + floor((rect.size.height - h) / 2.0), rect.size.width, h);
    [text drawInRect:r withAttributes:attrs];
}

static inline CGFloat TTRTextWidth(NSString* text, NSFont* font, CGFloat kern = 0.0) {
    return ceil([text sizeWithAttributes:TTRTextAttrs(font, TTR_TEXT, kern)].width);
}

static inline void TTRFillRounded(NSRect r, CGFloat radius, NSColor* color) {
    [color setFill];
    [[NSBezierPath bezierPathWithRoundedRect:r xRadius:radius yRadius:radius] fill];
}

static inline void TTRStrokeRounded(NSRect r, CGFloat radius, NSColor* color, CGFloat width = 1.0) {
    NSBezierPath* p = [NSBezierPath bezierPathWithRoundedRect:NSInsetRect(r, width / 2, width / 2) xRadius:radius yRadius:radius];
    p.lineWidth = width;
    [color setStroke];
    [p stroke];
}

static inline NSString* TTRNoteName(int pitch) {  // scientific pitch notation: MIDI 60 = C4, 69 = A4 = 440 Hz
    static NSString* const names[12] = {@"C", @"C#", @"D", @"D#", @"E", @"F", @"F#", @"G", @"G#", @"A", @"A#", @"B"};
    return [NSString stringWithFormat:@"%@%d", names[((pitch % 12) + 12) % 12], pitch / 12 - 1];
}

static inline double TTRPitchToHz(double pitch) { return 440.0 * pow(2.0, (pitch - 69.0) / 12.0); }

static inline BOOL TTRIsBlackKey(int pitch) {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// Note colour per instrument. The families get their own hues and the members of a family step through lightness, so a
// family reads at a glance (bass purple, guitars green, keys blue, voice pink, synths cyan, drums grey, ...).
#include <algorithm>

#include "core/Instruments.h"

static inline uint32_t TTRInstrumentRGB(int program) {
    static const uint32_t kFamily[] = {
        0x2EE6C5,  // None: the accent
        0x5B8CFF,  // Keys
        0xB7BDD6,  // Percussion
        0x3F6FD8,  // Organ
        0x52D68A,  // Guitar
        0xA45BFF,  // Bass
        0xFF5C7A,  // Strings
        0xFF8FB1,  // Voice
        0xFFB547,  // Brass
        0xFF7A4D,  // Reed
        0xC6E05A,  // Winds
        0x7FDDFF,  // Synth
        0x9AA4B5,  // Drums
    };
    const uint32_t base = kFamily[static_cast<int>(ttr::instrumentFamily(program))];
    if (program < 0 || program == ttr::kDrumProgram) return base;
    const double shade = 0.78 + 0.11 * double(program % 4);  // 0.78 .. 1.11 within a family
    auto channel = [&](int shift) { return uint32_t(std::min(255.0, double((base >> shift) & 0xFF) * shade)) << shift; };
    return channel(16) | channel(8) | channel(0);
}

// Darker rim of the same hue (keeps the bar readable on bright spectrum areas).
static inline uint32_t TTRDarkerRGB(uint32_t rgb) {
    auto channel = [&](int shift) { return (((rgb >> shift) & 0xFF) * 2 / 5) << shift; };
    return channel(16) | channel(8) | channel(0);
}
