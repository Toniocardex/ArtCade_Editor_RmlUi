#pragma once

#include "core/types.h"
#include "editor-native/model/tilemap_chunk_math.h"

#include <cstddef>
#include <optional>

namespace ArtCade::EditorNative {

// ============================================================================
// Canonical tileset sheet geometry.
//
// One authority for every margin/spacing/tile-size formula: slicing
// (tilesForSlicing), the Tile Palette's stamp builder, hit-testing, sheet
// rendering, selection highlight, pan-into-view and empty-mask mapping all go
// through these functions. Renderers may differ; geometry formulas may not.
// Pure model code: no raylib, no RmlUi, no coordinator, no filesystem.
// ============================================================================

struct TilesetGridGeometry {
    int imageWidth  = 0;
    int imageHeight = 0;
    int columns     = 0;
    int rows        = 0;
    int tileWidth   = 0;
    int tileHeight  = 0;
    int marginX     = 0;
    int marginY     = 0;
    int spacingX    = 0;
    int spacingY    = 0;
};

// Integer rect in sheet (source image) pixel space.
struct TilesetGridRect {
    int x = 0;
    int y = 0;
    int width  = 0;
    int height = 0;
};

// nullopt when not even one tile fits (same acceptance rule as
// computeTilesetSlicing, which this wraps as the single columns/rows
// authority).
std::optional<TilesetGridGeometry> computeTilesetGridGeometry(
    const TilesetSlicing& slicing, int imageWidth, int imageHeight);

// Sheet pixel -> grid cell (cellX = column, cellY = row). nullopt for pixels
// in the margins, in the spacing gutters between tiles, or outside the grid.
std::optional<TilemapCellCoord> tilesetGridCellAtSheetPixel(
    const TilesetGridGeometry& geometry, int pixelX, int pixelY);

// Grid cell -> the tile's source rect on the sheet. The cell is not bounds-
// checked (callers clamp/validate); the formula is the one tilesForSlicing
// slices with.
TilesetGridRect tilesetSourceRectForGridCell(const TilesetGridGeometry& geometry,
                                             TilemapCellCoord cell);

// Grid cell -> row-major linear index (the order tilesForSlicing emits tiles
// in). nullopt outside the grid.
std::optional<std::size_t> tilesetLinearIndexForGridCell(
    const TilesetGridGeometry& geometry, TilemapCellCoord cell);

// Reverse mapping: a tile's authored rect -> its grid cell, accepted only on
// exact agreement with tilesetSourceRectForGridCell. A tile whose rect is not
// grid-aligned (hand-edited data) simply has no cell: it is never part of the
// sheet grid, so palette hit-testing and stamps skip it.
std::optional<TilemapCellCoord> tilesetGridCellForTileRect(
    const TilesetGridGeometry& geometry, const TileDefinition& tile);

// Same exact-division mapping when the image dimensions (and therefore the
// column/row counts) are unknown - the ProjectDocument stores no pixel sizes,
// so provenance resolved coordinator-side uses this unbounded form. Anywhere
// a full geometry exists, the bounded overload above delegates here and adds
// the bounds check; the division formula lives once.
std::optional<TilemapCellCoord> tilesetGridCellForTileRectUnbounded(
    const TilesetSlicing& slicing, const TileDefinition& tile);

} // namespace ArtCade::EditorNative
