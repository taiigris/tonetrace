#import "ui/TTRHeatTiles.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Types.h"

namespace {
constexpr int kTilePixels = 128;       // device pixels per tile (width)
constexpr long kKeepTilesAround = 3;   // tiles kept left and right of the visible ones
constexpr float kLiveReferenceStepDb = 3.0f;  // coarse: every step re-renders all visible tiles of a growing map

// Highest value the live reference may take: rounded up to a step so a slowly rising peak keeps the tiles valid.
float quantizedReference(float peak) {
    if (peak <= 0.0f) return peak;
    const float db = 20.0f * std::log10(peak);
    return std::pow(10.0f, std::ceil(db / kLiveReferenceStepDb) * kLiveReferenceStepDb / 20.0f);
}
}  // namespace

void TTRHeatTileCache::clear() {
    for (auto& entry : tiles_) if (entry.second.image) CGImageRelease(entry.second.image);
    tiles_.clear();
    key_ = Key{};
    owner_.reset();
}

CGImageRef TTRHeatTileCache::render(const TTRHeatParams& p, long index, double spanSec, double tileWidth, double contentHeight, Tile* out) const {
    TTRHeatParams tile = p;
    tile.scrollSec = double(index) * spanSec;
    tile.scrollY = 0.0;
    tile.width = tileWidth;
    tile.height = contentHeight;
    out->bandTop = 0.0;
    out->bandHeight = contentHeight;
    if (p.linePoints) return TTRRenderLines(tile, &out->bandTop, &out->bandHeight);
    return TTRRenderHeatmap(tile);
}

void TTRHeatTileCache::draw(CGContextRef ctx, const TTRHeatParams& params, std::shared_ptr<const void> owner, uint64_t generation, bool isLive) {
    if (params.scale <= 0.0 || params.pxPerSec <= 0.0 || params.hopSec <= 0.0) return;
    TTRHeatParams p = params;
    if (isLive) p.reference = quantizedReference(p.reference);
    Key key;
    key.data = p.linePoints ? (const void*)p.linePoints : (const void*)p.data; key.generation = generation; key.hopSec = p.hopSec; key.pxPerSec = p.pxPerSec; key.rowH = p.rowH;
    key.scale = p.scale; key.gain = p.gain; key.range = p.range; key.reference = p.reference; key.tuning = p.tuningCents; key.fundamentals = p.fundamentals; key.hideDrums = p.hideDrums;
    if (!(key == key_)) {
        clear();
        key_ = key;
        owner_ = std::move(owner);
    }

    const double tileWidth = double(kTilePixels) / p.scale, spanSec = tileWidth / p.pxPerSec;
    const double contentHeight = std::ceil(double(ttr::kNumNotes) * p.rowH * p.scale) / p.scale;
    const long first = long(std::floor(p.scrollSec / spanSec)), last = long(std::floor((p.scrollSec + p.width / p.pxPerSec) / spanSec));
    const double originY = std::round(-p.scrollY * p.scale) / p.scale;

    for (long index = first; index <= last; ++index) {
        if (index < 0) continue;
        const double startSec = double(index) * spanSec;
        const int endFrame = int(std::ceil((startSec + spanSec) / p.hopSec)) + 1;  // last frame this tile can read
        const int startFrame = int(std::floor(startSec / p.hopSec + 0.5));
        const int framesUsed = (p.data || p.linePoints) && startFrame < p.frames ? std::min(p.frames, endFrame) : 0;  // tiles beyond the data stay valid as they are
        Tile& tile = tiles_[index];
        if (tile.framesUsed != framesUsed) {  // (a tile without lines has no image but is still up to date)
            if (tile.image) CGImageRelease(tile.image);
            tile.image = render(p, index, spanSec, tileWidth, contentHeight, &tile);
            tile.framesUsed = framesUsed;
            ++rendered_;
        }
        if (!tile.image) continue;
        const double x = std::round((startSec - p.scrollSec) * p.pxPerSec * p.scale) / p.scale;
        CGContextSaveGState(ctx);
        CGContextSetInterpolationQuality(ctx, kCGInterpolationNone);
        CGContextTranslateCTM(ctx, x, originY + tile.bandTop + tile.bandHeight);  // the view is flipped, images are not
        CGContextScaleCTM(ctx, 1, -1);
        CGContextDrawImage(ctx, CGRectMake(0, 0, tileWidth, tile.bandHeight), tile.image);
        CGContextRestoreGState(ctx);
    }
    for (auto it = tiles_.begin(); it != tiles_.end();) {  // drop what scrolled far away
        if (it->first < first - kKeepTilesAround || it->first > last + kKeepTilesAround) {
            if (it->second.image) CGImageRelease(it->second.image);
            it = tiles_.erase(it);
        } else {
            ++it;
        }
    }
}
