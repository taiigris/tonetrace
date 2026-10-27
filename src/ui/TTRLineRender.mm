// Glowing line map: every spectral peak is a white-hot horizontal streak at its exact pitch with a coloured shine, light adds up
// where streaks overlap. Rendered into cached tile bitmaps from the precomputed gauss profiles and colour maps of render/GlowProfile.
#import "ui/TTRHeatmap.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Types.h"
#include "render/GlowProfile.h"

namespace {

constexpr float kMinBrightness = 0.05f;
constexpr float kWarmBoost = 1.15f;        // overall brightness of the streaks
constexpr float kCoolBelow = 0.14f;        // weaker lines are thin, pale blue-violet
constexpr float kCoolBase = 0.08f, kCoolGain = 1.3f, kCoolMax = 0.34f;  // half as bright as in the first 4.2
constexpr float kCoolRelativeFloor = 0.08f;  // partials below this share of the frame's strongest peak are not drawn
constexpr float kCoolTexture = 2.5f, kCoolMinimum = 0.04f;
constexpr float kFlicker = 0.14f, kSwell = 0.30f;  // texture on top of the real level: +-12 % per frame, +-22 % swelling over ~6 frames
constexpr int kSwellFrames = 6;
constexpr int kLookBackFrames = 3;         // a line may skip this many frames (gap bridging of the tracker)
constexpr int kLiveMinAge = 2;
constexpr float kNegligible = 0.003f;
constexpr uint32_t kMapBackground = 0x08090B;
constexpr CGFloat kOctaveLineAlpha = 0.075, kEfLineAlpha = 0.03;

CGFloat component(uint32_t rgb, int shift) { return CGFloat((rgb >> shift) & 0xFF) / 255.0; }

void fillRect(CGContextRef ctx, CGRect r, uint32_t rgb, CGFloat alpha = 1.0) {
    CGContextSetRGBFillColor(ctx, component(rgb, 16), component(rgb, 8), component(rgb, 0), alpha);
    CGContextFillRect(ctx, r);
}

float hash01(uint32_t frame, uint32_t track) {
    uint32_t h = frame * 2654435761u ^ track * 40503u;
    h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
    return float(h & 0xFFFF) / 32767.5f - 1.0f;  // -1..1
}

// Same frame and track always give the same texture, so a re-rendered tile looks like the one it replaces.
float flickerOf(int frame, int track) {
    const uint32_t block = uint32_t(frame) / kSwellFrames;
    const float t = float(uint32_t(frame) % kSwellFrames) / kSwellFrames, s = t * t * (3.0f - 2.0f * t);
    const float swell = hash01(block, uint32_t(track)) * (1.0f - s) + hash01(block + 1, uint32_t(track)) * s;
    return 1.0f + kFlicker * hash01(uint32_t(frame), uint32_t(track) + 7u) + kSwell * swell;
}

// One piece of a line between two frames (view points); a line without a predecessor is a short dash.
struct Piece { float xa, xb, ya, yb, ba, bb; bool hasPrev, isCool; };

class GlowCanvas {
public:
    GlowCanvas(int w, int h, double scale, const ttr::GlowProfiles& profiles) : w_(w), h_(h), scale_(scale), profiles_(profiles), warm_(size_t(w) * size_t(h), 0.0f), cool_(size_t(w) * size_t(h), 0.0f) {}

    // `top` is the view y (points) of the first pixel row.
    void draw(const Piece& p, double top) {
        const double spanDev = double(p.xb - p.xa) * scale_;
        const float weight = float(std::min(1.0, std::max(spanDev, 0.0)));
        const int ca = int(std::floor(double(p.xa) * scale_)), cb = int(std::floor(double(p.xb) * scale_));
        const int first = p.hasPrev ? std::min(ca + 1, cb) : ca;
        for (int x = std::max(first, 0); x <= std::min(cb, w_ - 1); ++x) {
            const double t = spanDev > 1e-6 ? std::clamp((double(x) + 0.5 - double(p.xa) * scale_) / spanDev, 0.0, 1.0) : 1.0;
            const float b = float(p.ba + (p.bb - p.ba) * t);
            const double cy = ((double(p.ya) + (double(p.yb) - double(p.ya)) * t) - top) * scale_;
            splat(x, cy, b, weight, p.isCool);
        }
    }

    std::vector<uint32_t> toPixels() const {
        std::vector<uint32_t> out(warm_.size(), 0u);
        for (size_t i = 0; i < out.size(); ++i) {
            const float wv = warm_[i], cv = cool_[i];
            if (wv < kNegligible && cv < kNegligible) continue;
            const uint32_t a = ttr::glowLookup(ttr::glowWarmLut(), wv), c = ttr::glowLookup(ttr::glowCoolLut(), cv);
            const uint32_t r = std::min(255u, ((a >> 16) & 0xFF) + ((c >> 16) & 0xFF)), g = std::min(255u, ((a >> 8) & 0xFF) + ((c >> 8) & 0xFF)), bl = std::min(255u, (a & 0xFF) + (c & 0xFF));
            out[i] = (std::max({r, g, bl}) << 24) | (r << 16) | (g << 8) | bl;  // premultiplied: light on a transparent tile
        }
        return out;
    }

private:
    void splat(int x, double cy, float b, float weight, bool isCool) {
        const int i0 = int(std::floor(cy));
        const int phase = ttr::GlowProfiles::phaseOf(float(cy - double(i0)));
        const int cls = ttr::GlowProfiles::classOf(b);
        const int r = isCool ? profiles_.coolRadius() : profiles_.radius(cls);
        const float* taps = isCool ? profiles_.coolTaps(phase) : profiles_.taps(cls, phase);
        const float amp = (isCool ? std::min(kCoolMax, kCoolBase + kCoolGain * b) : b) * weight;  // (cool: b is already the textured level)
        std::vector<float>& plane = isCool ? cool_ : warm_;
        for (int y = std::max(0, i0 - r); y <= std::min(h_ - 1, i0 + r); ++y) plane[size_t(y) * size_t(w_) + size_t(x)] += amp * taps[y - i0 + r];
    }

    int w_, h_;
    double scale_;
    const ttr::GlowProfiles& profiles_;
    std::vector<float> warm_, cool_;
};

}  // namespace

// The piano-roll rows of the visible part, drawn by the view in every frame (cheap) so the cached line tiles stay transparent.
// Almost black: only the octave lines at C and a very faint one between E and F.
void TTRDrawRowGrid(CGContextRef ctx, const TTRHeatParams& p) {
    fillRect(ctx, CGRectMake(0, 0, p.width, p.height), kMapBackground);
    const int top = std::max(0, int(std::floor(p.scrollY / p.rowH))), bottom = std::min(ttr::kNumNotes - 1, int(std::floor((p.scrollY + p.height) / p.rowH)));
    const CGFloat hair = 1.0 / p.scale;
    for (int row = top; row <= bottom; ++row) {
        const int pc = (ttr::kMaxNote - row) % 12;
        if (pc != 0 && pc != 5) continue;
        const CGFloat y = double(row) * p.rowH - p.scrollY;
        fillRect(ctx, CGRectMake(0, y + p.rowH - hair, p.width, hair), 0xFFFFFF, pc == 0 ? kOctaveLineAlpha : kEfLineAlpha);
    }
}

uint32_t TTRRowColor(int pitch) { (void)pitch; return kMapBackground; }

CGImageRef TTRRenderLines(const TTRHeatParams& p, double* bandTop, double* bandHeight) {
    *bandTop = 0.0;
    *bandHeight = 0.0;
    const double hop = p.hopSec, centreRow = ttr::kMaxNote + 0.5, shift = double(p.tuningCents) / 100.0;
    const int first = std::max(0, int(std::floor(p.scrollSec / hop)) - kLookBackFrames - 1);
    const int last = std::min(p.frames - 1, int(std::ceil((p.scrollSec + p.width / p.pxPerSec) / hop)) + 1);
    auto xOf = [&](int frame) { return float((double(frame) * hop - p.scrollSec) * p.pxPerSec); };
    auto yOf = [&](float midi) { return float((centreRow - (double(midi) - shift)) * p.rowH); };
    auto visible = [&](const ttr::LinePoint& pt) {
        if (pt.track < 0 || (pt.flags & ttr::kLineHidden)) return false;
        if (p.hideDrums && (pt.flags & ttr::kLineDrum)) return false;
        return !(p.isLive && pt.age < kLiveMinAge);
    };

    std::vector<float> frameMax(size_t(std::max(0, last - first + 1)), 0.0f);  // strongest visible peak per frame (reference for the partials)
    for (int f = first; f <= last; ++f) {
        const ttr::LinePoint* row = &p.linePoints[size_t(f) * ttr::kLineSlots];
        for (int i = 0; i < p.lineCounts[size_t(f)]; ++i)
            if (visible(row[i])) frameMax[size_t(f - first)] = std::max(frameMax[size_t(f - first)], row[i].level);
    }
    // brightness of a point (0: not drawn) and whether it belongs to the thin pale partials
    auto shade = [&](const ttr::LinePoint& pt, int frame, bool* isCool) {
        const float level = ttr::lineBrightness(pt, p.reference, p.gain, p.range, p.fundamentals);
        if (level < kMinBrightness) return 0.0f;
        const float texture = flickerOf(frame, pt.track);
        *isCool = level < kCoolBelow || (p.fundamentals && (pt.flags & ttr::kLineHarmonic));
        if (!*isCool) return std::min(1.0f, level * texture * kWarmBoost);
        if (pt.level < kCoolRelativeFloor * frameMax[size_t(std::clamp(frame, first, last) - first)]) return 0.0f;
        const float b = std::clamp(level * (1.0f + kCoolTexture * (texture - 1.0f)), 0.0f, 1.0f);  // irregular traces
        return b < kCoolMinimum ? 0.0f : b;
    };

    std::vector<Piece> pieces;
    const float dashHalf = float(std::max(0.5, hop * p.pxPerSec) / 2.0);
    float minY = 1.0e9f, maxY = -1.0e9f;
    for (int f = first; f <= last; ++f) {
        const ttr::LinePoint* row = &p.linePoints[size_t(f) * ttr::kLineSlots];
        for (int i = 0; i < p.lineCounts[size_t(f)]; ++i) {
            const ttr::LinePoint& pt = row[i];
            if (!visible(pt)) continue;
            bool isCool = false;
            const float b = shade(pt, f, &isCool);
            if (b <= 0.0f) continue;
            Piece piece = {xOf(f) - dashHalf, xOf(f) + dashHalf, yOf(pt.midi), yOf(pt.midi), b, b, false, isCool};  // isolated point: a short dash
            for (int g = f - 1; g >= std::max(0, f - kLookBackFrames); --g) {
                const ttr::LinePoint* prev = &p.linePoints[size_t(g) * ttr::kLineSlots];
                bool found = false;
                for (int k = 0; k < p.lineCounts[size_t(g)] && !found; ++k)
                    if (prev[k].track == pt.track) {
                        bool prevCool = false;
                        const float pb = g >= first ? shade(prev[k], g, &prevCool) : b;
                        piece = {xOf(g), xOf(f), yOf(prev[k].midi), piece.yb, pb > 0.0f ? pb : b, b, true, isCool};
                        found = true;
                    }
                if (found) break;
            }
            minY = std::min({minY, piece.ya, piece.yb});
            maxY = std::max({maxY, piece.ya, piece.yb});
            pieces.push_back(piece);
        }
    }
    if (pieces.empty()) return nullptr;  // nothing to draw in this tile

    const int W = int(std::ceil(p.width * p.scale));
    if (W <= 0) return nullptr;
    const ttr::GlowProfiles profiles(p.scale);
    const CGFloat pad = std::ceil(double(profiles.maxRadius()) / p.scale) + 1.0;
    const CGFloat top = std::max(0.0, std::floor(double(minY) - pad)), bottom = std::min(p.height, std::ceil(double(maxY) + pad));
    const int H = int(std::ceil((bottom - top) * p.scale));
    if (H <= 0) return nullptr;
    *bandTop = top;
    *bandHeight = bottom - top;
    GlowCanvas canvas(W, H, p.scale, profiles);
    for (const Piece& piece : pieces) canvas.draw(piece, top);
    std::vector<uint32_t> pixels = canvas.toPixels();

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), size_t(W), size_t(H), 8, size_t(W) * 4, space, kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(space);
    if (!ctx) return nullptr;
    CGImageRef image = CGBitmapContextCreateImage(ctx);  // copies the pixels
    CGContextRelease(ctx);
    return image;
}
