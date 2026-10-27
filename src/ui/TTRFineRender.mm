// Spectrum view: the whole spectrum on the fine pitch grid (kFineBinsPerNote bins per semitone) as a heat map.
// Per tile: (1) a shade plane per frame and fine bin (level -> shade, drums and overtones dimmed), (2) a column pass that
// resamples frames to pixel columns, (3) a row pass that resamples fine bins to pixel rows (shifted by the tuning).
// Where a pixel is narrower than a frame or a bin the values are interpolated, where it is wider the strongest one wins.
#import "ui/TTRHeatmap.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Types.h"

namespace {

constexpr float kDimmedBrightness = 0.3f;  // drums and overtones with Hide drums / Fundamentals
constexpr int kOvertoneHalfWidthBins = 2;  // an overtone dims its peak: +-2 fine bins (+-1/4 semitone)

struct Tap { int first = 0, second = 0; float weight = 0.0f; bool isRange = false; };  // blend of two, or the maximum of first..second

// How a pixel reads a source axis: `position` is its centre in source units, `span` its width in source units.
Tap tapFor(double position, double span, int count) {
    Tap t;
    if (span <= 1.0) {
        const int i = int(std::floor(position));
        t.first = std::clamp(i, 0, count - 1);
        t.second = std::clamp(i + 1, 0, count - 1);
        t.weight = float(position - i);
        return t;
    }
    t.isRange = true;
    t.first = std::clamp(int(std::ceil(position - span / 2)), 0, count - 1);
    t.second = std::clamp(int(std::floor(position + span / 2)), t.first, count - 1);
    return t;
}

inline uint8_t sampleTap(const Tap& t, uint8_t a, uint8_t b) {  // used for blends
    return uint8_t(std::lround(float(a) * (1.0f - t.weight) + float(b) * t.weight));
}

// Shade of every fine bin of one frame: level through the display curve, times the dimming of drums and overtones.
void shadeFrame(const TTRHeatParams& p, const uint8_t* lut, int frame, uint8_t* out) {
    const uint8_t* codes = &p.fine[size_t(frame) * ttr::kFineBins];
    float keep[ttr::kFineBins];
    std::fill(keep, keep + ttr::kFineBins, 1.0f);
    if (p.hideDrums && p.data && p.harmonic && frame < p.harmonicFrames) {
        const float* raw = &p.data[size_t(frame) * ttr::kNumNotes];
        const float* harm = &p.harmonic[size_t(frame) * ttr::kNumNotes];
        for (int j = 0; j < ttr::kFineBins; ++j) {
            const int n = j / ttr::kFineBinsPerNote;
            const float tonal = raw[n] > 0.0f ? std::min(1.0f, harm[n] / raw[n]) : 1.0f;
            keep[j] = kDimmedBrightness + (1.0f - kDimmedBrightness) * tonal;
        }
    }
    if (p.fundamentals && p.linePoints && p.lineCounts && frame < p.frames) {
        const ttr::LinePoint* points = &p.linePoints[size_t(frame) * ttr::kLineSlots];
        for (int i = 0; i < p.lineCounts[frame]; ++i) {
            if (!(points[i].flags & ttr::kLineHarmonic)) continue;
            const int centre = int(std::lround((double(points[i].midi) - (ttr::kMinNote - 0.5)) * ttr::kFineBinsPerNote - 0.5));
            for (int j = std::max(0, centre - kOvertoneHalfWidthBins); j <= std::min(ttr::kFineBins - 1, centre + kOvertoneHalfWidthBins); ++j) keep[j] = std::min(keep[j], kDimmedBrightness);
        }
    }
    for (int j = 0; j < ttr::kFineBins; ++j) out[j] = uint8_t(std::lround(float(lut[codes[j]]) * keep[j]));
}

}  // namespace

CGImageRef TTRRenderFine(const TTRHeatParams& p) {
    const int W = int(std::ceil(p.width * p.scale)), H = int(std::ceil(p.height * p.scale));
    if (W <= 0 || H <= 0 || !p.fine || p.frames <= 0 || p.hopSec <= 0.0) return nullptr;
    std::vector<uint32_t> pixels(size_t(W) * size_t(H), TTRHeatColor(0));

    uint8_t lut[256];
    for (int c = 0; c < 256; ++c) lut[c] = uint8_t(std::lround(TTRHeatShade(ttr::fineCodeToAmplitude(uint8_t(c)), p.reference, p.gain, p.range) * 255.0f));

    // columns: which frames feed which pixel column
    const double framesPerPixel = 1.0 / (p.pxPerSec * p.scale * p.hopSec);
    std::vector<Tap> columns(static_cast<size_t>(W));
    int firstFrame = p.frames, lastFrame = -1;
    for (int x = 0; x < W; ++x) {
        const double t = p.scrollSec + (double(x) + 0.5) / p.scale / p.pxPerSec;
        if (t / p.hopSec < -1.0 || t / p.hopSec > p.frames) { columns[size_t(x)].weight = -1.0f; continue; }  // outside the recording
        columns[size_t(x)] = tapFor(t / p.hopSec, framesPerPixel, p.frames);
        firstFrame = std::min(firstFrame, columns[size_t(x)].first);
        lastFrame = std::max(lastFrame, columns[size_t(x)].second);
    }
    if (lastFrame < firstFrame) return nullptr;

    const int frames = lastFrame - firstFrame + 1;
    std::vector<uint8_t> plane(size_t(frames) * size_t(ttr::kFineBins));
    for (int f = 0; f < frames; ++f) shadeFrame(p, lut, firstFrame + f, &plane[size_t(f) * size_t(ttr::kFineBins)]);

    // column pass: colv[bin][x]
    std::vector<uint8_t> colv(size_t(ttr::kFineBins) * size_t(W), 0);
    for (int x = 0; x < W; ++x) {
        const Tap& t = columns[size_t(x)];
        if (t.weight < 0.0f) continue;
        const uint8_t* a = &plane[size_t(t.first - firstFrame) * size_t(ttr::kFineBins)];
        const uint8_t* b = &plane[size_t(t.second - firstFrame) * size_t(ttr::kFineBins)];
        for (int j = 0; j < ttr::kFineBins; ++j) {
            uint8_t v;
            if (t.isRange) {
                v = 0;
                for (int f = t.first; f <= t.second; ++f) v = std::max(v, plane[size_t(f - firstFrame) * size_t(ttr::kFineBins) + size_t(j)]);
            } else {
                v = sampleTap(t, a[j], b[j]);
            }
            colv[size_t(j) * size_t(W) + size_t(x)] = v;
        }
    }

    // row pass: bins -> pixel rows (the grid is shifted by the tuning, row 0 of the content is the top of kMaxNote)
    const double binsPerPixel = double(ttr::kFineBinsPerNote) / (p.rowH * p.scale);
    for (int y = 0; y < H; ++y) {
        const double contentY = (double(y) + 0.5) / p.scale + p.scrollY;
        const double midi = ttr::kMaxNote + 0.5 - contentY / p.rowH + double(p.tuningCents) / 100.0;
        const double bin = (midi - (ttr::kMinNote - 0.5)) * ttr::kFineBinsPerNote - 0.5;
        if (bin < -1.0 || bin > ttr::kFineBins) continue;
        const Tap t = tapFor(bin, binsPerPixel, ttr::kFineBins);
        const uint8_t* a = &colv[size_t(t.first) * size_t(W)];
        const uint8_t* b = &colv[size_t(t.second) * size_t(W)];
        uint32_t* line = &pixels[size_t(y) * size_t(W)];
        for (int x = 0; x < W; ++x) {
            uint8_t v;
            if (t.isRange) {
                v = 0;
                for (int j = t.first; j <= t.second; ++j) v = std::max(v, colv[size_t(j) * size_t(W) + size_t(x)]);
            } else {
                v = sampleTap(t, a[x], b[x]);
            }
            line[x] = TTRHeatColor(v);
        }
    }

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), size_t(W), size_t(H), 8, size_t(W) * 4, space, kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
    if (ctx) CGContextRelease(ctx);
    CGColorSpaceRelease(space);
    return image;
}
