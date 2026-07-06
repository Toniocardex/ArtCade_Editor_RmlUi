#include "editor-native/model/tilemap_stroke_math.h"

#include <cmath>

namespace ArtCade::EditorNative {

std::vector<TilemapCellCoord> rasterizeCellLine(TilemapCellCoord from, TilemapCellCoord to) {
    std::vector<TilemapCellCoord> result;

    int x0 = from.cellX;
    int y0 = from.cellY;
    const int x1 = to.cellX;
    const int y1 = to.cellY;

    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;

    while (true) {
        result.push_back(TilemapCellCoord{x0, y0});
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * error;
        if (e2 >= dy) {
            if (x0 == x1) break;
            error += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            if (y0 == y1) break;
            error += dx;
            y0 += sy;
        }
    }
    return result;
}

std::vector<TilemapCellChange> normalizePaintStrokeChanges(
    const std::unordered_map<std::int64_t, TilemapCellChange>& changes) {
    std::vector<TilemapCellChange> result;
    result.reserve(changes.size());
    for (const auto& [key, change] : changes) {
        if (!(change.before == change.after)) result.push_back(change);
    }
    return result;
}

} // namespace ArtCade::EditorNative
