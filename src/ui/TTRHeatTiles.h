// Cache of heatmap bitmap tiles: fixed-width strips of the whole note range, rendered once and blitted on every frame.
// Tiles are only re-rendered when gain / range / data / zoom change or when new frames reach a tile that is still growing.
#pragma once
#import <CoreGraphics/CoreGraphics.h>

#include <cstdint>
#include <memory>
#include <unordered_map>

#include "ui/TTRHeatmap.h"

class TTRHeatTileCache {
public:
    TTRHeatTileCache() = default;
    ~TTRHeatTileCache() { clear(); }
    TTRHeatTileCache(const TTRHeatTileCache&) = delete;
    TTRHeatTileCache& operator=(const TTRHeatTileCache&) = delete;

    // Draws the visible part of the heatmap into `ctx` (view coordinates, flipped view: y down, row 0 on top).
    // `generation` identifies the recording that `params.data` belongs to; `isLive` quantises the reference level
    // so a rising peak while recording does not invalidate every tile.
    // `owner` keeps the memory behind params.data alive while tiles of it are cached (an address is never reused).
    void draw(CGContextRef ctx, const TTRHeatParams& params, std::shared_ptr<const void> owner, uint64_t generation, bool isLive);
    void clear();
    int tilesRendered() const { return rendered_; }  // total since creation (tests, diagnostics)
    size_t tileCount() const { return tiles_.size(); }

private:
    struct Tile { CGImageRef image = nullptr; int framesUsed = -1; };
    struct Key {
        const float* data = nullptr;
        uint64_t generation = ~0ull;
        double hopSec = 0, pxPerSec = 0, rowH = 0, scale = 0;
        float gain = 0, range = 0, reference = 0;
        bool operator==(const Key& o) const {
            return data == o.data && generation == o.generation && hopSec == o.hopSec && pxPerSec == o.pxPerSec && rowH == o.rowH && scale == o.scale &&
                   gain == o.gain && range == o.range && reference == o.reference;
        }
    };
    CGImageRef render(const TTRHeatParams& params, long index, double spanSec, double tileWidth, double contentHeight) const;

    std::unordered_map<long, Tile> tiles_;
    Key key_;
    std::shared_ptr<const void> owner_;
    int rendered_ = 0;
};
