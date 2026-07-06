#pragma once

#include "editor-native/model/tilemap_cell_access.h"
#include "editor-native/model/tilemap_chunk_math.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace ArtCade::EditorNative {

// Bresenham-style integer line rasterization between two cells, inclusive
// of both endpoints, each cell exactly once, consecutive cells always
// 8-connected. Used to fill in cells crossed during a fast mouse move
// between two pointer-move samples so a fast stroke leaves no gaps.
// from == to returns a single-element vector.
std::vector<TilemapCellCoord> rasterizeCellLine(TilemapCellCoord from, TilemapCellCoord to);

// Pointer-up normalization: drops any change whose before == after (a cell
// touched during the stroke but left in its original state, e.g. painted
// then erased back, or painted with the tile it already had) so the
// resulting delta only contains cells that actually changed. Pure and
// directly testable without simulating pointer input.
std::vector<TilemapCellChange> normalizePaintStrokeChanges(
    const std::unordered_map<std::int64_t, TilemapCellChange>& changes);

} // namespace ArtCade::EditorNative
