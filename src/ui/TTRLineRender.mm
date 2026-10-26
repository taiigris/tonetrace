// Thin-line map: a piano-roll grid (black-key rows darker, C rows brighter, drawn by the view) with one sharp line per spectral peak at its exact pitch (cached tiles).
#import "ui/TTRHeatmap.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Types.h"

namespace {

constexpr CGFloat kLineWidth = 2.0, kBandPadding = 3.0;
constexpr int kBrightnessBuckets = 20;
constexpr float kMinBrightness = 0.05f;
constexpr int kLookBackFrames = 3;      // a line may skip this many frames (gap bridging of the tracker)
constexpr int kLiveMinAge = 2;
constexpr uint32_t kWhiteKeyRow = 0x0D0E12, kBlackKeyRow = 0x050506, kCRow = 0x14161C;

CGFloat component(uint32_t rgb, int shift) { return CGFloat((rgb >> shift) & 0xFF) / 255.0; }

void fillRect(CGContextRef ctx, CGRect r, uint32_t rgb, CGFloat alpha = 1.0) {
    CGContextSetRGBFillColor(ctx, component(rgb, 16), component(rgb, 8), component(rgb, 0), alpha);
    CGContextFillRect(ctx, r);
}

bool isBlackKey(int pitch) {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

// brightness 0..1 -> colour: steel blue for weak lines, white for the strongest
void lineColor(float b, CGFloat* r, CGFloat* g, CGFloat* bl, CGFloat* a) {
    const float t = std::clamp(b, 0.0f, 1.0f);
    const float k = std::pow(t, 0.6f);
    *r = (0.30f + 0.70f * k);
    *g = (0.46f + 0.52f * k);
    *bl = (0.70f + 0.28f * k);
    *a = std::clamp(0.35f + 1.0f * t, 0.0f, 1.0f);
}

struct Segment { CGPoint a, b; };

}  // namespace

// The piano-roll rows of the visible part, drawn by the view in every frame (cheap) so the cached line tiles stay transparent.
void TTRDrawRowGrid(CGContextRef ctx, const TTRHeatParams& p) {
    fillRect(ctx, CGRectMake(0, 0, p.width, p.height), kBlackKeyRow);
    const int top = std::max(0, int(std::floor(p.scrollY / p.rowH))), bottom = std::min(ttr::kNumNotes - 1, int(std::floor((p.scrollY + p.height) / p.rowH)));
    const CGFloat hair = 1.0 / p.scale;
    for (int row = top; row <= bottom; ++row) {
        const int pitch = ttr::kMaxNote - row;
        const CGFloat y = double(row) * p.rowH - p.scrollY;
        fillRect(ctx, CGRectMake(0, y, p.width, p.rowH), TTRRowColor(pitch));
        fillRect(ctx, CGRectMake(0, y + p.rowH - hair, p.width, hair), 0xFFFFFF, pitch % 12 == 0 ? 0.16 : 0.055);  // line under each row, brighter under C
    }
}

uint32_t TTRRowColor(int pitch) { return isBlackKey(pitch) ? kBlackKeyRow : pitch % 12 == 0 ? kCRow : kWhiteKeyRow; }

CGImageRef TTRRenderLines(const TTRHeatParams& p, double* bandTop, double* bandHeight) {
    *bandTop = 0.0;
    *bandHeight = 0.0;
    const double hop = p.hopSec, centreRow = ttr::kMaxNote + 0.5, shift = double(p.tuningCents) / 100.0;
    const int first = std::max(0, int(std::floor(p.scrollSec / hop)) - kLookBackFrames - 1);
    const int last = std::min(p.frames - 1, int(std::ceil((p.scrollSec + p.width / p.pxPerSec) / hop)) + 1);
    auto xOf = [&](int frame) { return (double(frame) * hop - p.scrollSec) * p.pxPerSec; };
    auto yOf = [&](float midi) { return (centreRow - (double(midi) - shift)) * p.rowH; };
    auto visible = [&](const ttr::LinePoint& pt) {
        if (pt.track < 0 || (pt.flags & ttr::kLineHidden)) return false;
        if (p.hideDrums && (pt.flags & ttr::kLineDrum)) return false;
        return !(p.isLive && pt.age < kLiveMinAge);
    };

    std::vector<Segment> buckets[kBrightnessBuckets];
    const CGFloat dash = std::max(2.0, hop * p.pxPerSec);
    for (int f = first; f <= last; ++f) {
        const ttr::LinePoint* row = &p.linePoints[size_t(f) * ttr::kLineSlots];
        for (int i = 0; i < p.lineCounts[size_t(f)]; ++i) {
            const ttr::LinePoint& pt = row[i];
            if (!visible(pt)) continue;
            const float b = ttr::lineBrightness(pt, p.reference, p.gain, p.range, p.fundamentals);
            if (b < kMinBrightness) continue;
            const CGPoint now = CGPointMake(xOf(f), yOf(pt.midi));
            Segment seg = {CGPointMake(now.x - dash / 2.0, now.y), CGPointMake(now.x + dash / 2.0, now.y)};  // isolated point: a short dash
            for (int g = f - 1; g >= std::max(0, f - kLookBackFrames); --g) {
                const ttr::LinePoint* prev = &p.linePoints[size_t(g) * ttr::kLineSlots];
                bool found = false;
                for (int k = 0; k < p.lineCounts[size_t(g)] && !found; ++k)
                    if (prev[k].track == pt.track) { seg = {CGPointMake(xOf(g), yOf(prev[k].midi)), now}; found = true; }
                if (found) break;
            }
            buckets[std::min(kBrightnessBuckets - 1, int(b * kBrightnessBuckets))].push_back(seg);
        }
    }
    CGFloat minY = 1.0e9, maxY = -1.0e9;
    for (const auto& bucket : buckets)
        for (const Segment& seg : bucket) { minY = std::min({minY, seg.a.y, seg.b.y}); maxY = std::max({maxY, seg.a.y, seg.b.y}); }
    if (minY > maxY) return nullptr;  // nothing to draw in this tile
    const CGFloat top = std::max(0.0, std::floor(minY - kBandPadding)), bottom = std::min(p.height, std::ceil(maxY + kBandPadding));
    *bandTop = top;
    *bandHeight = bottom - top;
    const int W = int(std::ceil(p.width * p.scale)), H = int(std::ceil((bottom - top) * p.scale));
    if (W <= 0 || H <= 0) return nullptr;
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(nullptr, size_t(W), size_t(H), 8, 0, space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(space);
    if (!ctx) return nullptr;
    CGContextScaleCTM(ctx, p.scale, p.scale);
    CGContextTranslateCTM(ctx, 0, bottom - top);  // y down, like the view; the band starts at `top`
    CGContextScaleCTM(ctx, 1, -1);
    CGContextTranslateCTM(ctx, 0, -top);
    CGContextSetLineWidth(ctx, kLineWidth);
    CGContextSetLineCap(ctx, kCGLineCapRound);
    CGContextSetAllowsAntialiasing(ctx, true);
    for (int k = 0; k < kBrightnessBuckets; ++k) {
        if (buckets[k].empty()) continue;
        CGFloat r, g, bl, a;
        lineColor((float(k) + 0.5f) / kBrightnessBuckets, &r, &g, &bl, &a);
        CGContextSetRGBStrokeColor(ctx, r, g, bl, a);
        CGContextStrokeLineSegments(ctx, reinterpret_cast<const CGPoint*>(buckets[k].data()), buckets[k].size() * 2);
    }
    CGImageRef image = CGBitmapContextCreateImage(ctx);
    CGContextRelease(ctx);
    return image;
}
