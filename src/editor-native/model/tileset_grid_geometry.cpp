#include "editor-native/model/tileset_grid_geometry.h"

#include "editor-native/model/tileset_slicing.h"

namespace ArtCade::EditorNative {

std::optional<TilesetGridGeometry> computeTilesetGridGeometry(
    const TilesetSlicing& slicing, int imageWidth, int imageHeight) {
    const TilesetSliceResult slice = computeTilesetSlicing(imageWidth, imageHeight, slicing);
    if (slice.tileCount <= 0) return std::nullopt;

    TilesetGridGeometry geometry;
    geometry.imageWidth  = imageWidth;
    geometry.imageHeight = imageHeight;
    geometry.columns     = slice.columns;
    geometry.rows        = slice.rows;
    geometry.tileWidth   = slicing.tileWidth;
    geometry.tileHeight  = slicing.tileHeight;
    geometry.marginX     = slicing.marginX;
    geometry.marginY     = slicing.marginY;
    geometry.spacingX    = slicing.spacingX;
    geometry.spacingY    = slicing.spacingY;
    return geometry;
}

std::optional<TilemapCellCoord> tilesetGridCellAtSheetPixel(
    const TilesetGridGeometry& geometry, int pixelX, int pixelY) {
    if (geometry.columns <= 0 || geometry.rows <= 0) return std::nullopt;
    const int localX = pixelX - geometry.marginX;
    const int localY = pixelY - geometry.marginY;
    if (localX < 0 || localY < 0) return std::nullopt;

    const int stepX = geometry.tileWidth + geometry.spacingX;
    const int stepY = geometry.tileHeight + geometry.spacingY;
    const int col = localX / stepX;
    const int row = localY / stepY;
    if (col >= geometry.columns || row >= geometry.rows) return std::nullopt;
    // Spacing gutters between tiles belong to no cell.
    if (localX % stepX >= geometry.tileWidth) return std::nullopt;
    if (localY % stepY >= geometry.tileHeight) return std::nullopt;
    return TilemapCellCoord{col, row};
}

TilesetGridRect tilesetSourceRectForGridCell(const TilesetGridGeometry& geometry,
                                             TilemapCellCoord cell) {
    TilesetGridRect rect;
    rect.x = geometry.marginX + cell.cellX * (geometry.tileWidth + geometry.spacingX);
    rect.y = geometry.marginY + cell.cellY * (geometry.tileHeight + geometry.spacingY);
    rect.width  = geometry.tileWidth;
    rect.height = geometry.tileHeight;
    return rect;
}

std::optional<std::size_t> tilesetLinearIndexForGridCell(
    const TilesetGridGeometry& geometry, TilemapCellCoord cell) {
    if (cell.cellX < 0 || cell.cellX >= geometry.columns) return std::nullopt;
    if (cell.cellY < 0 || cell.cellY >= geometry.rows) return std::nullopt;
    return static_cast<std::size_t>(cell.cellY) * static_cast<std::size_t>(geometry.columns)
         + static_cast<std::size_t>(cell.cellX);
}

std::optional<TilemapCellCoord> tilesetGridCellForTileRect(
    const TilesetGridGeometry& geometry, const TileDefinition& tile) {
    if (tile.width != geometry.tileWidth || tile.height != geometry.tileHeight) {
        return std::nullopt;
    }
    const int stepX = geometry.tileWidth + geometry.spacingX;
    const int stepY = geometry.tileHeight + geometry.spacingY;
    const int localX = tile.x - geometry.marginX;
    const int localY = tile.y - geometry.marginY;
    if (localX < 0 || localY < 0) return std::nullopt;
    if (localX % stepX != 0 || localY % stepY != 0) return std::nullopt;
    const TilemapCellCoord cell{localX / stepX, localY / stepY};
    if (!tilesetLinearIndexForGridCell(geometry, cell)) return std::nullopt;
    return cell;
}

} // namespace ArtCade::EditorNative
