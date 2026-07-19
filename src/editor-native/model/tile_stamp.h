#pragma once

#include "core/types.h"
#include "editor-native/model/tilemap_chunk_math.h"
#include "editor-native/model/tilemap_region_math.h"
#include "editor-native/model/tileset_grid_geometry.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace ArtCade::EditorNative {

// ============================================================================
// Multi-tile stamp: the Tile Palette's selection unit.
//
// A stamp is an N x M block of tiles selected from ONE tileset. It carries its
// source tileset id because tile ids are only unique within a tileset - two
// tilesets can both own a "tile-1", and a stamp from tileset A must never
// paint into a tilemap that meanwhile switched to tileset B. Provenance
// (sourceColumn/sourceRow) locates the block on the sheet for the palette's
// persistent highlight and pan-into-view, including fully-empty edge rows.
// Pure model code: no raylib, no RmlUi, no coordinator, no caches.
// ============================================================================

// Safety cap shared with the paint operations the stamp feeds.
inline constexpr std::size_t kMaxTileStampCells = kMaxTilePaintOperationCells;

struct TilemapTileStamp {
    AssetId sourceTilesetAssetId;

    // Grid cell of the block's top-left corner on the source sheet.
    // -1/-1 = unknown provenance (a picker-made stamp whose tile is not
    // grid-aligned); the highlight then falls back to id matching.
    int sourceColumn = -1;
    int sourceRow = -1;

    int width = 1;
    int height = 1;

    // Row-major, size == width * height. nullopt = hole: that position never
    // modifies the destination cell (it is a skip, NOT an erase).
    std::vector<std::optional<TileId>> tiles;
};

// A 1x1 stamp. Provenance may be unknown (-1/-1) when the caller cannot map
// the tile onto the sheet grid.
TilemapTileStamp makeSingleTileStamp(AssetId tilesetAssetId, TileId tileId,
                                     int sourceColumn = -1, int sourceRow = -1);

// Structural validity: dimensions >= 1, overflow-safe width*height within
// kMaxTileStampCells, slot count matching, at least one non-hole slot, and a
// non-empty source tileset id. Says nothing about whether the ids still exist
// in that tileset - that is the coordinator's validation/reconcile job.
bool stampIsValid(const TilemapTileStamp& stamp);

// First non-hole tile id. Compact readouts and fallbacks only - never the
// authority of a multi-tile selection.
std::optional<TileId> stampPrimaryTileId(const TilemapTileStamp& stamp);

// Memberwise equality (change detection, e.g. pan-selected-into-view). Not
// operator== on the struct: the editor's own comparison conventions
// (sameTilesetSlicing, sameTileDefinitions) keep these as named functions.
bool sameTileStamp(const TilemapTileStamp& a, const TilemapTileStamp& b);

// -- Brush semantics ---------------------------------------------------------
// Every interpolated brush position is the anchor (top-left) of one whole
// N x M footprint: a single click with a 2x2 stamp paints 4 cells.
struct TileStampPlacement {
    TilemapCellCoord cell;
    TileId tileId;
};
// Holes are omitted; placements whose coordinate would overflow the int32
// cell range are dropped (hard boundary, same rule as tryOffsetCell).
std::vector<TileStampPlacement> stampPlacementsAt(const TilemapTileStamp& stamp,
                                                  TilemapCellCoord placementAnchor);

// -- Rectangle / Fill semantics ----------------------------------------------
// Modular repetition: the tile for a destination cell is
// stamp[(cell - regionAnchor) mod (width, height)] with Euclidean modulo, so
// regions dragged up/left of the anchor wrap correctly. nullopt = hole.
std::optional<TileId> stampPatternTileAt(const TilemapTileStamp& stamp,
                                         TilemapCellCoord regionAnchor,
                                         TilemapCellCoord cell);

// -- Marquee -> stamp --------------------------------------------------------
// Builds the stamp for the inclusive grid region spanned by the two corners
// (order-independent), clamped to the sheet grid. Tiles are located by exact
// rect agreement with the canonical geometry; a grid cell with no matching
// tile is a hole. `emptyFlags`, when given, must align with tileset.tiles
// (one flag per tile, true = fully transparent) - empty tiles become holes.
// Returns nullopt when the region misses the grid entirely, the mask is
// misaligned, the block would exceed kMaxTileStampCells, or every slot would
// be a hole (an all-empty marquee selects nothing).
std::optional<TilemapTileStamp> buildTileStampFromRegion(
    const TilesetAsset& tileset, const TilesetGridGeometry& geometry,
    int col0, int row0, int col1, int row1,
    const std::vector<bool>* emptyFlags);

} // namespace ArtCade::EditorNative
