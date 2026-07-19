#include "editor-native/model/tile_stamp.h"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace ArtCade::EditorNative {

namespace {

// Euclidean modulo over the int64 delta: never negative, never overflows.
int euclideanMod(std::int64_t value, int modulus) {
    const std::int64_t m = static_cast<std::int64_t>(modulus);
    std::int64_t r = value % m;
    if (r < 0) r += m;
    return static_cast<int>(r);
}

} // namespace

TilemapTileStamp makeSingleTileStamp(AssetId tilesetAssetId, TileId tileId,
                                     int sourceColumn, int sourceRow) {
    TilemapTileStamp stamp;
    stamp.sourceTilesetAssetId = std::move(tilesetAssetId);
    stamp.sourceColumn = sourceColumn;
    stamp.sourceRow = sourceRow;
    stamp.width = 1;
    stamp.height = 1;
    stamp.tiles = {std::optional<TileId>{std::move(tileId)}};
    return stamp;
}

bool stampIsValid(const TilemapTileStamp& stamp) {
    if (stamp.sourceTilesetAssetId.empty()) return false;
    if (stamp.width <= 0 || stamp.height <= 0) return false;

    // Overflow-safe: divide before ever multiplying width * height.
    const std::size_t w = static_cast<std::size_t>(stamp.width);
    const std::size_t h = static_cast<std::size_t>(stamp.height);
    if (w > kMaxTileStampCells / h) return false;
    const std::size_t count = w * h;
    if (count > kMaxTileStampCells) return false;

    if (stamp.tiles.size() != count) return false;
    for (const std::optional<TileId>& tile : stamp.tiles) {
        if (tile) return true;
    }
    return false;   // all holes: selects nothing
}

std::optional<TileId> stampPrimaryTileId(const TilemapTileStamp& stamp) {
    for (const std::optional<TileId>& tile : stamp.tiles) {
        if (tile) return tile;
    }
    return std::nullopt;
}

std::vector<TileStampPlacement> stampPlacementsAt(const TilemapTileStamp& stamp,
                                                  TilemapCellCoord placementAnchor) {
    std::vector<TileStampPlacement> placements;
    if (!stampIsValid(stamp)) return placements;
    placements.reserve(stamp.tiles.size());
    for (int row = 0; row < stamp.height; ++row) {
        for (int col = 0; col < stamp.width; ++col) {
            const std::optional<TileId>& tile =
                stamp.tiles[static_cast<std::size_t>(row) * stamp.width + col];
            if (!tile) continue;
            const std::optional<TilemapCellCoord> cell =
                tryOffsetCell(placementAnchor, col, row);
            if (!cell) continue;   // int32 grid edge: hard boundary
            placements.push_back(TileStampPlacement{*cell, *tile});
        }
    }
    return placements;
}

std::optional<TileId> stampPatternTileAt(const TilemapTileStamp& stamp,
                                         TilemapCellCoord regionAnchor,
                                         TilemapCellCoord cell) {
    if (!stampIsValid(stamp)) return std::nullopt;
    const std::int64_t dx = static_cast<std::int64_t>(cell.cellX)
                          - static_cast<std::int64_t>(regionAnchor.cellX);
    const std::int64_t dy = static_cast<std::int64_t>(cell.cellY)
                          - static_cast<std::int64_t>(regionAnchor.cellY);
    const int col = euclideanMod(dx, stamp.width);
    const int row = euclideanMod(dy, stamp.height);
    return stamp.tiles[static_cast<std::size_t>(row) * stamp.width + col];
}

std::optional<TilemapTileStamp> buildTileStampFromRegion(
    const TilesetAsset& tileset, const TilesetGridGeometry& geometry,
    int col0, int row0, int col1, int row1,
    const std::vector<bool>* emptyFlags) {
    if (geometry.columns <= 0 || geometry.rows <= 0) return std::nullopt;
    // A misaligned mask must never be indexed (its classification would apply
    // to the wrong tiles); an absent mask is fine - no empty filtering.
    if (emptyFlags && emptyFlags->size() != tileset.tiles.size()) return std::nullopt;

    int minCol = std::min(col0, col1);
    int maxCol = std::max(col0, col1);
    int minRow = std::min(row0, row1);
    int maxRow = std::max(row0, row1);
    // Clamp to the grid; a region entirely outside selects nothing.
    if (maxCol < 0 || maxRow < 0) return std::nullopt;
    if (minCol >= geometry.columns || minRow >= geometry.rows) return std::nullopt;
    minCol = std::max(minCol, 0);
    minRow = std::max(minRow, 0);
    maxCol = std::min(maxCol, geometry.columns - 1);
    maxRow = std::min(maxRow, geometry.rows - 1);

    const std::size_t width  = static_cast<std::size_t>(maxCol - minCol) + 1;
    const std::size_t height = static_cast<std::size_t>(maxRow - minRow) + 1;
    if (width > kMaxTileStampCells / height) return std::nullopt;

    // Grid cell -> tile, by exact rect agreement with the canonical geometry.
    // Built once per call; also remembers each tile's index for the mask.
    std::unordered_map<std::int64_t, std::size_t> tileByCell;
    for (std::size_t i = 0; i < tileset.tiles.size(); ++i) {
        const std::optional<TilemapCellCoord> cell =
            tilesetGridCellForTileRect(geometry, tileset.tiles[i]);
        if (cell) tileByCell.emplace(packTilemapCellCoord(*cell), i);
    }

    TilemapTileStamp stamp;
    stamp.sourceTilesetAssetId = tileset.assetId;
    stamp.sourceColumn = minCol;
    stamp.sourceRow = minRow;
    stamp.width = static_cast<int>(width);
    stamp.height = static_cast<int>(height);
    stamp.tiles.resize(width * height);
    for (int row = minRow; row <= maxRow; ++row) {
        for (int col = minCol; col <= maxCol; ++col) {
            const auto it = tileByCell.find(packTilemapCellCoord(TilemapCellCoord{col, row}));
            if (it == tileByCell.end()) continue;                       // no tile here: hole
            if (emptyFlags && (*emptyFlags)[it->second]) continue;      // empty tile: hole
            stamp.tiles[static_cast<std::size_t>(row - minRow) * width + (col - minCol)] =
                tileset.tiles[it->second].id;
        }
    }
    if (!stampIsValid(stamp)) return std::nullopt;   // every slot a hole
    return stamp;
}

} // namespace ArtCade::EditorNative
