#import "ui/TTRHeatmap.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Types.h"

namespace {

constexpr float kMinRangeDb = 18.0f, kRangeSpanDb = 50.0f, kGainSpanDb = 40.0f;
constexpr float kShadeGamma = 2.2f;  // darkens weak energy so the strong lines stand out
constexpr double kRowGapPoints = 1.0;
constexpr uint8_t kBackgroundShade = 6;  // shades below this show the plain row background

uint32_t pack(float r, float g, float b) {
    return 0xFF000000u | (uint32_t(std::clamp(r, 0.0f, 255.0f)) << 16) | (uint32_t(std::clamp(g, 0.0f, 255.0f)) << 8) | uint32_t(std::clamp(b, 0.0f, 255.0f));
}

std::vector<uint32_t> makeRamp() {
    struct Stop { float at, r, g, b; };
    static const Stop stops[] = {{0.00f, 14, 16, 24}, {0.22f, 52, 20, 112}, {0.48f, 196, 30, 66}, {0.74f, 255, 196, 32}, {1.00f, 255, 255, 255}};
    std::vector<uint32_t> ramp(256);
    for (int i = 0; i < 256; ++i) {
        const float x = float(i) / 255.0f;
        size_t k = 0;
        while (k + 2 < sizeof(stops) / sizeof(stops[0]) && x > stops[k + 1].at) ++k;
        const float t = std::clamp((x - stops[k].at) / (stops[k + 1].at - stops[k].at), 0.0f, 1.0f);
        ramp[size_t(i)] = pack(stops[k].r + (stops[k + 1].r - stops[k].r) * t, stops[k].g + (stops[k + 1].g - stops[k].g) * t,
                               stops[k].b + (stops[k + 1].b - stops[k].b) * t);
    }
    return ramp;
}

uint32_t darker(uint32_t c, float keep) {
    return pack(float((c >> 16) & 0xFF) * keep, float((c >> 8) & 0xFF) * keep, float(c & 0xFF) * keep);
}

bool isBlackKey(int pitch) {
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

struct Column { int first, last; };

}  // namespace

uint32_t TTRHeatColor(uint8_t shade) {
    static const std::vector<uint32_t> ramp = makeRamp();
    return ramp[shade];
}

uint32_t TTRDarkenColor(uint32_t color, float keep) { return darker(color, keep); }

float TTRHeatShade(float energy, float reference, float gain, float range) {
    if (energy <= 0.0f || reference <= 0.0f) return 0.0f;
    const float rangeDb = kMinRangeDb + std::clamp(range, 0.0f, 1.0f) * kRangeSpanDb;
    const float gainDb = (std::clamp(gain, 0.0f, 1.0f) - 0.5f) * kGainSpanDb;
    const float db = 20.0f * std::log10(energy / reference) + gainDb;
    return std::pow(std::clamp(1.0f + db / rangeDb, 0.0f, 1.0f), kShadeGamma);
}

CGImageRef TTRRenderHeatmap(const TTRHeatParams& p) {
    const int W = int(std::ceil(p.width * p.scale)), H = int(std::ceil(p.height * p.scale));
    if (W <= 0 || H <= 0) return nullptr;
    static const std::vector<uint32_t> ramp = makeRamp();
    const uint32_t rowBase[2] = {pack(18, 21, 28), pack(12, 14, 19)};  // white key, black key
    std::vector<uint32_t> pixels(size_t(W) * size_t(H), rowBase[0]);

    // time -> frame range of every pixel column
    std::vector<Column> columns(static_cast<size_t>(W));
    int firstFrame = p.frames, lastFrame = -1;
    for (int x = 0; x < W; ++x) {
        const double t0 = p.scrollSec + (double(x) / p.scale) / p.pxPerSec, t1 = p.scrollSec + (double(x + 1) / p.scale) / p.pxPerSec;
        int a = int(std::floor(t0 / p.hopSec + 0.5)), b = std::max(a, int(std::floor(t1 / p.hopSec + 0.5 - 1e-9)));
        if (b < 0 || a >= p.frames || !p.data) { columns[size_t(x)] = {1, 0}; continue; }
        a = std::max(a, 0);
        b = std::min(b, p.frames - 1);
        columns[size_t(x)] = {a, b};
        firstFrame = std::min(firstFrame, a);
        lastFrame = std::max(lastFrame, b);
    }
    // visible note rows
    const int topRow = std::max(0, int(std::floor(p.scrollY / p.rowH)));
    const int bottomRow = std::min(ttr::kNumNotes - 1, int(std::floor((p.scrollY + p.height) / p.rowH)));
    const int rows = bottomRow - topRow + 1;
    const bool hasCells = lastFrame >= firstFrame && rows > 0;

    // shade per (frame, row) cell: one log per cell instead of one per pixel
    std::vector<uint8_t> cells;
    if (hasCells) {
        const int frames = lastFrame - firstFrame + 1;
        cells.resize(size_t(frames) * size_t(rows));
        for (int f = 0; f < frames; ++f)
            for (int r = 0; r < rows; ++r) {
                const int note = ttr::kNumNotes - 1 - (topRow + r);  // row 0 = highest pitch
                const float e = p.data[size_t(firstFrame + f) * ttr::kNumNotes + size_t(note)];
                cells[size_t(f) * size_t(rows) + size_t(r)] = uint8_t(std::lround(TTRHeatShade(e, p.reference, p.gain, p.range) * 255.0f));
            }
    }

    // One pixel line per note row (and its darker gap line) is built once and copied to every pixel row of that semitone.
    std::vector<uint32_t> lineNormal(static_cast<size_t>(W));
    std::vector<uint32_t> lineGap(static_cast<size_t>(W));
    int builtRow = -2;
    for (int y = 0; y < H; ++y) {
        const double contentY = double(y) / p.scale + p.scrollY;
        const int row = int(std::floor(contentY / p.rowH));
        uint32_t* line = &pixels[size_t(y) * size_t(W)];
        if (row < 0 || row >= ttr::kNumNotes) { std::fill(line, line + W, pack(10, 11, 15)); continue; }
        if (row != builtRow) {
            builtRow = row;
            const int pitch = ttr::kMaxNote - row;
            const uint32_t base = rowBase[isBlackKey(pitch) ? 1 : 0];
            const bool rowHasCells = hasCells && row >= topRow && row <= bottomRow;
            const size_t cellRow = size_t(std::max(0, row - topRow));
            for (int x = 0; x < W; ++x) {
                uint8_t shade = 0;
                const Column c = columns[size_t(x)];
                if (rowHasCells)
                    for (int f = c.first; f <= c.last; ++f) shade = std::max(shade, cells[size_t(f - firstFrame) * size_t(rows) + cellRow]);
                const uint32_t color = shade < kBackgroundShade ? base : ramp[shade];
                lineNormal[size_t(x)] = color;
                lineGap[size_t(x)] = darker(color, 0.45f);
            }
        }
        const bool isGap = (double(row + 1) * p.rowH - contentY) <= kRowGapPoints;
        std::copy(isGap ? lineGap.begin() : lineNormal.begin(), isGap ? lineGap.end() : lineNormal.end(), line);
    }

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef ctx = CGBitmapContextCreate(pixels.data(), size_t(W), size_t(H), 8, size_t(W) * 4, space,
                                             kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little);
    CGImageRef image = ctx ? CGBitmapContextCreateImage(ctx) : nullptr;
    if (ctx) CGContextRelease(ctx);
    CGColorSpaceRelease(space);
    return image;
}
