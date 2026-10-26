#import "ui/TTRLogo.h"

#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace {

constexpr CGFloat kMarkSize = 512, kWordW = 1590, kWordH = 180, kWordX = 20, kWordY = 40;
constexpr CGFloat kLetterX = 260, kLetterY = 60, kLetterW = 1330, kLetterH = 140;
constexpr CGFloat kIconSize = 1024;

struct Bar { CGFloat x, y, w, h; };
const Bar kTealBars[] = {{64, 216, 40, 80}, {120, 164, 40, 184}, {176, 112, 40, 288}, {204, 136, 244, 32}, {204, 240, 140, 32}, {204, 344, 244, 32}};
const Bar kAccentBar = {368, 240, 80, 32};

// Letters of tonetrace-wordmark.svg (SVG path data; M m L l H h V v C c Z z are enough for them).
const char* const kLetterPaths[] = {
    "M260 60h110v24h-43v116h-24V84h-43Z",
    "M415 60h110v140H415Zm24 24v92h62V84Z",
    "M570 200V60h24l62 96V60h24v140h-24l-62-96v96Z",
    "M725 60h100v24h-76v34h66v24h-66v34h76v24H725Z",
    "M870 60h110v24h-43v116h-24V84h-43Z",
    "M1025 200V60h67c29 0 43 15 43 43v16c0 20-8 33-25 39l31 42h-30l-28-39h-34v39Zm24-116v53h40c15 0 22-7 22-21v-11c0-14-7-21-22-21Z",
    "M1180 200l50-140h20l50 140h-26l-11-32h-46l-11 32Zm45-56h30l-15-45Z",
    "M1445 60v24h-60c-17 0-26 9-26 26v40c0 17 9 26 26 26h60v24h-65c-30 0-45-15-45-45v-50c0-30 15-45 45-45Z",
    "M1490 60h100v24h-76v34h66v24h-66v34h76v24h-100Z",
};

bool isNumberStart(char c) { return (c >= '0' && c <= '9') || c == '-' || c == '.'; }

// Minimal SVG path parser for the subset above.
CGPathRef createPath(const char* d) {
    CGMutablePathRef path = CGPathCreateMutable();
    CGPoint cur = CGPointZero, start = CGPointZero;
    char cmd = 0;
    const char* p = d;
    auto num = [&]() {
        while (*p == ' ' || *p == ',') ++p;
        char* end = nullptr;
        const double v = strtod(p, &end);
        p = end;
        return CGFloat(v);
    };
    while (*p) {
        while (*p == ' ' || *p == ',') ++p;
        if (!*p) break;
        if (!isNumberStart(*p)) cmd = *p++;
        const bool rel = cmd >= 'a';
        const CGFloat ox = rel ? cur.x : 0, oy = rel ? cur.y : 0;
        switch (cmd) {
            case 'M': case 'm': cur = CGPointMake(ox + num(), oy + num()); start = cur; CGPathMoveToPoint(path, nullptr, cur.x, cur.y); cmd = rel ? 'l' : 'L'; break;
            case 'L': case 'l': cur = CGPointMake(ox + num(), oy + num()); CGPathAddLineToPoint(path, nullptr, cur.x, cur.y); break;
            case 'H': case 'h': cur.x = ox + num(); CGPathAddLineToPoint(path, nullptr, cur.x, cur.y); break;
            case 'V': case 'v': cur.y = oy + num(); CGPathAddLineToPoint(path, nullptr, cur.x, cur.y); break;
            case 'C': case 'c': {
                const CGPoint c1 = CGPointMake(ox + num(), oy + num()), c2 = CGPointMake(ox + num(), oy + num()), e = CGPointMake(ox + num(), oy + num());
                CGPathAddCurveToPoint(path, nullptr, c1.x, c1.y, c2.x, c2.y, e.x, e.y);
                cur = e;
                break;
            }
            case 'Z': case 'z': CGPathCloseSubpath(path); cur = start; break;
            default: CGPathRelease(path); return nullptr;
        }
    }
    return path;
}

CGPathRef lettersPath() {
    static CGPathRef all = [] {
        CGMutablePathRef m = CGPathCreateMutable();
        for (const char* d : kLetterPaths)
            if (CGPathRef part = createPath(d)) { CGPathAddPath(m, nullptr, part); CGPathRelease(part); }
        return (CGPathRef)m;
    }();
    return all;
}

CGColorRef srgb(uint32_t rgb) {
    static CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    const CGFloat c[4] = {((rgb >> 16) & 0xFF) / 255.0, ((rgb >> 8) & 0xFF) / 255.0, (rgb & 0xFF) / 255.0, 1.0};
    return CGColorCreate(space, c);
}

void fillRoundedBar(CGContextRef ctx, const Bar& b, CGColorRef color) {
    const CGFloat r = std::min<CGFloat>(b.w, b.h) / 2;  // every bar of the mark is a full pill
    CGPathRef path = CGPathCreateWithRoundedRect(CGRectMake(b.x, b.y, b.w, b.h), r, r, nullptr);
    CGContextSetFillColorWithColor(ctx, color);
    CGContextAddPath(ctx, path);
    CGContextFillPath(ctx);
    CGPathRelease(path);
}

void drawMarkUnits(CGContextRef ctx) {
    CGColorRef teal = srgb(0x2EE6C5), orange = srgb(0xFF9F43);
    for (const Bar& b : kTealBars) fillRoundedBar(ctx, b, teal);
    fillRoundedBar(ctx, kAccentBar, orange);
    CGColorRelease(teal);
    CGColorRelease(orange);
}

void fillLetters(CGContextRef ctx, CGColorRef color) {
    CGContextSetFillColorWithColor(ctx, color);
    CGContextAddPath(ctx, lettersPath());
    CGContextEOFillPath(ctx);
}

}  // namespace

const CGFloat kTTRWordmarkAspect = kWordW / kWordH;
const CGFloat kTTRLetteringAspect = kLetterW / kLetterH;

void TTRDrawMark(CGContextRef ctx, CGRect rect) {
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, rect.origin.x, rect.origin.y);
    CGContextScaleCTM(ctx, rect.size.width / kMarkSize, rect.size.height / kMarkSize);
    drawMarkUnits(ctx);
    CGContextRestoreGState(ctx);
}

void TTRDrawWordmark(CGContextRef ctx, CGRect rect) {
    const CGFloat s = std::min<CGFloat>(rect.size.width / kWordW, rect.size.height / kWordH);
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, rect.origin.x, rect.origin.y + (rect.size.height - kWordH * s) / 2);
    CGContextScaleCTM(ctx, s, s);
    CGContextTranslateCTM(ctx, -kWordX, -kWordY);
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, 0, 14);
    CGContextScaleCTM(ctx, 0.45, 0.45);
    drawMarkUnits(ctx);
    CGContextRestoreGState(ctx);
    CGColorRef text = srgb(0xE6E9EF);
    fillLetters(ctx, text);
    CGColorRelease(text);
    CGContextRestoreGState(ctx);
}

void TTRDrawLettering(CGContextRef ctx, CGRect rect, CGColorRef color) {
    const CGFloat s = std::min<CGFloat>(rect.size.width / kLetterW, rect.size.height / kLetterH);
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, rect.origin.x + (rect.size.width - kLetterW * s) / 2, rect.origin.y + (rect.size.height - kLetterH * s) / 2);
    CGContextScaleCTM(ctx, s, s);
    CGContextTranslateCTM(ctx, -kLetterX, -kLetterY);
    fillLetters(ctx, color);
    CGContextRestoreGState(ctx);
}

void TTRDrawIconTile(CGContextRef ctx, CGRect rect) {
    CGContextSaveGState(ctx);
    CGContextTranslateCTM(ctx, rect.origin.x, rect.origin.y);
    CGContextScaleCTM(ctx, rect.size.width / kIconSize, rect.size.height / kIconSize);

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGColorRef top = srgb(0x1B1F26), bottom = srgb(0x121418), edge = srgb(0x2A3038);
    CFArrayRef colors = CFArrayCreate(nullptr, (const void*[]){top, bottom}, 2, &kCFTypeArrayCallBacks);
    CGGradientRef gradient = CGGradientCreateWithColors(space, colors, (CGFloat[]){0, 1});
    CGPathRef tile = CGPathCreateWithRoundedRect(CGRectMake(100, 100, 824, 824), 185, 185, nullptr);
    CGContextSaveGState(ctx);
    CGContextAddPath(ctx, tile);
    CGContextClip(ctx);
    CGContextDrawLinearGradient(ctx, gradient, CGPointMake(164, 132), CGPointMake(860, 892), kCGGradientDrawsBeforeStartLocation | kCGGradientDrawsAfterEndLocation);
    CGContextRestoreGState(ctx);
    CGPathRef rim = CGPathCreateWithRoundedRect(CGRectMake(101, 101, 822, 822), 184, 184, nullptr);
    CGContextSetStrokeColorWithColor(ctx, edge);
    CGContextSetLineWidth(ctx, 2);
    CGContextAddPath(ctx, rim);
    CGContextStrokePath(ctx);

    CGContextTranslateCTM(ctx, 141, 141);
    CGContextScaleCTM(ctx, 1.45, 1.45);
    drawMarkUnits(ctx);

    CGPathRelease(rim);
    CGPathRelease(tile);
    CGGradientRelease(gradient);
    CFRelease(colors);
    CGColorRelease(top);
    CGColorRelease(bottom);
    CGColorRelease(edge);
    CGColorSpaceRelease(space);
    CGContextRestoreGState(ctx);
}
