// Mapping of the note rows to the pooled bins of the short-window (HPSS) spectrum.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "core/Types.h"

namespace ttr {

struct PooledPair { int first, second; };

// Pooled bins that cover each note's band: the one holding the centre frequency plus its nearer neighbour.
inline std::vector<PooledPair> noteToPooledBins(double pooledHz, int bins) {
    std::vector<PooledPair> map(kNumNotes);
    for (int i = 0; i < kNumNotes; ++i) {
        const double pos = 440.0 * std::pow(2.0, (kMinNote + i - 69) / 12.0) / pooledHz;
        const int b = std::min(bins - 1, int(pos));
        const int other = pos - b < 0.5 ? b - 1 : b + 1;
        map[size_t(i)] = {b, std::min(bins - 1, std::max(0, other))};
    }
    return map;
}

}  // namespace ttr
