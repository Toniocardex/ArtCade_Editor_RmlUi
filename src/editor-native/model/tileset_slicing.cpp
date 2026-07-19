#include "editor-native/model/tileset_slicing.h"

#include "editor-native/model/tileset_grid_geometry.h"

#include <string>
#include <unordered_set>

namespace ArtCade::EditorNative {

namespace {
bool sameRect(const TileDefinition& a, const TileDefinition& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}
} // namespace

TilesetSliceResult computeTilesetSlicing(int imageWidth, int imageHeight,
                                         const TilesetSlicing& slicing) {
    TilesetSliceResult result;
    if (imageWidth <= 0 || imageHeight <= 0) return result;
    if (slicing.tileWidth <= 0 || slicing.tileHeight <= 0) return result;
    if (slicing.marginX < 0 || slicing.marginY < 0) return result;
    if (slicing.spacingX < 0 || slicing.spacingY < 0) return result;

    const int usableW = imageWidth - slicing.marginX * 2;
    const int usableH = imageHeight - slicing.marginY * 2;
    if (usableW < slicing.tileWidth || usableH < slicing.tileHeight) return result;

    const int stepX = slicing.tileWidth + slicing.spacingX;
    const int stepY = slicing.tileHeight + slicing.spacingY;

    result.columns   = 1 + (usableW - slicing.tileWidth) / stepX;
    result.rows      = 1 + (usableH - slicing.tileHeight) / stepY;
    result.tileCount = result.columns * result.rows;
    result.remainderX = usableW - (result.columns * slicing.tileWidth
                                  + (result.columns - 1) * slicing.spacingX);
    result.remainderY = usableH - (result.rows * slicing.tileHeight
                                  + (result.rows - 1) * slicing.spacingY);
    return result;
}

std::vector<TileDefinition> tilesForSlicing(int imageWidth, int imageHeight,
                                            const TilesetSlicing& slicing) {
    // Rects come from the canonical grid geometry (tileset_grid_geometry.h):
    // hit-testing, stamps and the sheet renderer must agree with the slicer
    // on every margin/spacing formula, so none of them owns a private copy.
    std::vector<TileDefinition> tiles;
    const std::optional<TilesetGridGeometry> geometry =
        computeTilesetGridGeometry(slicing, imageWidth, imageHeight);
    if (!geometry) return tiles;

    tiles.reserve(static_cast<std::size_t>(geometry->columns) * geometry->rows);
    for (int row = 0; row < geometry->rows; ++row) {
        for (int col = 0; col < geometry->columns; ++col) {
            const TilesetGridRect rect =
                tilesetSourceRectForGridCell(*geometry, TilemapCellCoord{col, row});
            TileDefinition tile;
            tile.id     = "tile-" + std::to_string(tiles.size() + 1);
            tile.x      = rect.x;
            tile.y      = rect.y;
            tile.width  = rect.width;
            tile.height = rect.height;
            tiles.push_back(std::move(tile));
        }
    }
    return tiles;
}

std::vector<TileDefinition> reconcileTiles(const std::vector<TileDefinition>& oldTiles,
                                           const std::vector<TileDefinition>& newTiles) {
    // Every old id is reserved, kept or not: a fresh rect must never claim a
    // kept id (two tiles sharing one id) nor recycle a removed id (painted
    // cells still referencing it would silently show the new rect's content
    // instead of being detected as orphaned and cleared).
    std::unordered_set<std::string> usedIds;
    for (const TileDefinition& old : oldTiles) usedIds.insert(old.id);
    std::vector<TileDefinition> result;
    result.reserve(newTiles.size());
    for (const TileDefinition& fresh : newTiles) {
        TileDefinition tile = fresh;
        bool matched = false;
        for (const TileDefinition& old : oldTiles) {
            if (sameRect(old, fresh)) {
                tile.id = old.id;
                matched = true;
                break;
            }
        }
        if (!matched) {
            int n = 1;
            std::string candidate;
            do {
                candidate = "tile-" + std::to_string(n++);
            } while (usedIds.count(candidate) != 0);
            tile.id = candidate;
            usedIds.insert(tile.id);
        }
        result.push_back(std::move(tile));
    }
    return result;
}

bool sameTilesetSlicing(const TilesetSlicing& a, const TilesetSlicing& b) {
    return a.tileWidth == b.tileWidth && a.tileHeight == b.tileHeight
        && a.marginX == b.marginX && a.marginY == b.marginY
        && a.spacingX == b.spacingX && a.spacingY == b.spacingY;
}

bool sameTileDefinitions(const std::vector<TileDefinition>& a,
                         const std::vector<TileDefinition>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].id != b[i].id || !sameRect(a[i], b[i])) return false;
    }
    return true;
}

std::optional<int> adjacentTileIndex(int columns, int rows, int index, int dx, int dy) {
    if (columns <= 0 || rows <= 0) return std::nullopt;
    if (index < 0 || index >= columns * rows) return std::nullopt;
    const int col = index % columns + dx;
    const int row = index / columns + dy;
    if (col < 0 || col >= columns || row < 0 || row >= rows) return std::nullopt;
    return row * columns + col;
}

} // namespace ArtCade::EditorNative
