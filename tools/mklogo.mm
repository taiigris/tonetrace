// Renders the brand PNGs and the app icon iconset from the native CoreGraphics drawing (src/ui/TTRLogo.mm).
// Usage: tonetrace_mklogo <png-dir> <docs-logo.png>
//   <png-dir>/Tonetrace.iconset/icon_*.png   16..512 @1x/@2x for iconutil
//   <png-dir>/tonetrace-mark-512.png, tonetrace-wordmark.png (transparent), tonetrace-icon-1024.png
//   <docs-logo.png>                          wordmark on dark ground, 1200 x 300 (README)
// tools/make_icon.sh runs this and iconutil. The output only depends on the drawing code, so it is reproducible.
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "ui/TTRLogo.h"

namespace {

constexpr size_t kDocsW = 1200, kDocsH = 300;
constexpr CGFloat kDocsWordmarkW = 1000;
constexpr CGFloat kWordmarkPngW = 2400;

using Painter = void (^)(CGContextRef ctx, CGSize size);

// Renders into an sRGB RGBA bitmap with y pointing down and writes a PNG.
bool renderPng(NSString* path, size_t w, size_t h, Painter paint) {
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(nullptr, w, h, 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    if (!ctx) return false;
    CGContextTranslateCTM(ctx, 0, h);
    CGContextScaleCTM(ctx, 1, -1);
    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    paint(ctx, CGSizeMake(w, h));
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    CGImageDestinationRef dest = CGImageDestinationCreateWithURL((__bridge CFURLRef)[NSURL fileURLWithPath:path], (__bridge CFStringRef)UTTypePNG.identifier, 1, nullptr);
    bool ok = dest && image;
    if (ok) {
        CGImageDestinationAddImage(dest, image, nullptr);
        ok = CGImageDestinationFinalize(dest);
    }
    if (dest) CFRelease(dest);
    CGImageRelease(image);
    if (!ok) fprintf(stderr, "could not write %s\n", path.UTF8String);
    return ok;
}

}  // namespace

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        if (argc < 3) { fprintf(stderr, "usage: tonetrace_mklogo <png-dir> <docs-logo.png>\n"); return 2; }
        NSString* dir = [NSString stringWithUTF8String:argv[1]];
        NSString* iconset = [dir stringByAppendingPathComponent:@"Tonetrace.iconset"];
        [NSFileManager.defaultManager createDirectoryAtPath:iconset withIntermediateDirectories:YES attributes:nil error:nil];
        bool ok = true;

        Painter tile = ^(CGContextRef ctx, CGSize s) { TTRDrawIconTile(ctx, CGRectMake(0, 0, s.width, s.height)); };
        const size_t sizes[] = {16, 32, 128, 256, 512};
        for (size_t base : sizes) {
            ok &= renderPng([iconset stringByAppendingPathComponent:[NSString stringWithFormat:@"icon_%zux%zu.png", base, base]], base, base, tile);
            ok &= renderPng([iconset stringByAppendingPathComponent:[NSString stringWithFormat:@"icon_%zux%zu@2x.png", base, base]], base * 2, base * 2, tile);
        }
        ok &= renderPng([dir stringByAppendingPathComponent:@"tonetrace-icon-1024.png"], 1024, 1024, tile);
        ok &= renderPng([dir stringByAppendingPathComponent:@"tonetrace-mark-512.png"], 512, 512, ^(CGContextRef ctx, CGSize s) { TTRDrawMark(ctx, CGRectMake(0, 0, s.width, s.height)); });

        const size_t wordH = size_t(kWordmarkPngW / kTTRWordmarkAspect);
        ok &= renderPng([dir stringByAppendingPathComponent:@"tonetrace-wordmark.png"], size_t(kWordmarkPngW), wordH, ^(CGContextRef ctx, CGSize s) { TTRDrawWordmark(ctx, CGRectMake(0, 0, s.width, s.height)); });

        ok &= renderPng([NSString stringWithUTF8String:argv[2]], kDocsW, kDocsH, ^(CGContextRef ctx, CGSize s) {
            CGContextSetRGBFillColor(ctx, 0x12 / 255.0, 0x14 / 255.0, 0x18 / 255.0, 1);
            CGContextFillRect(ctx, CGRectMake(0, 0, s.width, s.height));
            const CGFloat h = kDocsWordmarkW / kTTRWordmarkAspect;
            TTRDrawWordmark(ctx, CGRectMake((s.width - kDocsWordmarkW) / 2, (s.height - h) / 2, kDocsWordmarkW, h));
        });
        printf("%s\n", ok ? "logo assets written" : "FAILED");
        return ok ? 0 : 1;
    }
}
