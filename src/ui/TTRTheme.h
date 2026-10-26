// Colours, fonts and small drawing helpers shared by all Tonetrace views.
#pragma once
#import <Cocoa/Cocoa.h>

static inline NSColor* TTRHex(uint32_t rgb, CGFloat alpha = 1.0) {
    return [NSColor colorWithSRGBRed:((rgb >> 16) & 0xFF) / 255.0 green:((rgb >> 8) & 0xFF) / 255.0 blue:(rgb & 0xFF) / 255.0 alpha:alpha];
}

#define TTR_BG TTRHex(0x121418)
#define TTR_PANEL TTRHex(0x1B1F26)
#define TTR_PANEL_RAISED TTRHex(0x252B35)
#define TTR_BORDER TTRHex(0x2C333E)
#define TTR_ACCENT TTRHex(0x2EE6C5)
#define TTR_SELECT TTRHex(0xFF9F43)
#define TTR_RECORD TTRHex(0xFF4D5E)
#define TTR_NOTE TTRHex(0x19E6A0)
#define TTR_TEXT TTRHex(0xE6E9EF)
#define TTR_DIM TTRHex(0x8A93A3)

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
