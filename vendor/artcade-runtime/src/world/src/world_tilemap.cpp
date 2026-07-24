#include "../include/world.h"

#include <cmath>

namespace ArtCade {

bool World::isSpaceFree(float x, float y, float w, float h) const {
    const auto& tm = activeTilemap_;
    if (tm.cols <= 0 || tm.rows <= 0 || tm.tileSize <= 0.f) return true;

    const float ts = tm.tileSize;
    const int c0 = static_cast<int>(std::floor(x / ts));
    const int r0 = static_cast<int>(std::floor(y / ts));
    const int c1 = static_cast<int>(std::floor((x + w) / ts));
    const int r1 = static_cast<int>(std::floor((y + h) / ts));

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            if (c < 0 || r < 0 || c >= tm.cols || r >= tm.rows) return false;
            const int idx = r * tm.cols + c;
            if (idx >= static_cast<int>(tm.data.size())) continue;
            const int tid = tm.data[idx];
            if (tid <= 0) continue;
            auto it = tileMeta_.find(tid);
            if (it != tileMeta_.end() && it->second.blocks) return false;
        }
    }
    return true;
}

} // namespace ArtCade
