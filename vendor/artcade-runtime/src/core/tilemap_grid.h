#ifndef TILEMAP_GRID_H
#define TILEMAP_GRID_H

// ---------------------------------------------------------------------------
// tilemap_grid — editor↔runtime contract for tilemap grid dimensions.
//
// Must stay in sync with editor/src/types/tilemap-grid.ts (same pattern as
// ProjectRuntimeSettings ↔ runtime-fingerprint.ts).
// ---------------------------------------------------------------------------

#include <cmath>
#include <algorithm>

namespace ArtCade {

struct TilemapGridLimits {
    int minCols = 8;
    int minRows = 6;
    int maxCols = 64;
    int maxRows = 48;
};

inline int tilemap_grid_clamp_dim(int value, int minVal, int maxVal) {
    return std::max(minVal, std::min(value, maxVal));
}

/**
 * Compute tilemap column/row counts from scene world size and cell size.
 * @param worldW scene width in world units (px)
 * @param worldH scene height in world units (px)
 * @param tileSize cell size in px (must be > 0; falls back to 32)
 * @param limits clamp range for cols/rows
 * @param outCols output column count
 * @param outRows output row count
 */
inline void tilemap_grid_dims_from_world(
    float worldW,
    float worldH,
    float tileSize,
    const TilemapGridLimits& limits,
    int& outCols,
    int& outRows)
{
    constexpr float kDefaultTileSize = 32.f;
    const float step = tileSize > 0.f ? tileSize : kDefaultTileSize;
    outCols = tilemap_grid_clamp_dim(
        static_cast<int>(std::lround(worldW / step)),
        limits.minCols,
        limits.maxCols);
    outRows = tilemap_grid_clamp_dim(
        static_cast<int>(std::lround(worldH / step)),
        limits.minRows,
        limits.maxRows);
}

} // namespace ArtCade

#endif // TILEMAP_GRID_H
