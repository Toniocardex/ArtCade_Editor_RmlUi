#pragma once

#include "core/types.h"

#include <vector>

namespace ArtCade::EditorNative {

struct TilesetSliceResult {
    int columns   = 0;
    int rows      = 0;
    int tileCount = 0;
    int remainderX = 0;   // leftover pixels on the right edge
    int remainderY = 0;   // leftover pixels on the bottom edge
};

// Derives columns/rows/tile count from the image's pixel dimensions and a
// pixel-size-first TilesetSlicing config (tile width/height authored
// directly, unlike the Sprite Animation Editor's count-first slicing).
// Rejects (all-zero result) only when not even one tile fits; otherwise
// floors so an imperfect division still slices, leaving the remainder
// uncovered rather than refusing.
TilesetSliceResult computeTilesetSlicing(int imageWidth, int imageHeight,
                                         const TilesetSlicing& slicing);

// The sliced tiles themselves, in column-major-per-row order (left to right,
// top to bottom), with sequential ids ("tile-1", "tile-2", ...). Stable-id
// preservation across a changed TilesetSlicing is not this function's job -
// see reconcileTiles below.
std::vector<TileDefinition> tilesForSlicing(int imageWidth, int imageHeight,
                                            const TilesetSlicing& slicing);

// Reconciles a fresh slice (from tilesForSlicing) against the tiles a
// TilesetAsset already had: a new tile whose rect exactly matches an old
// tile's rect keeps that old tile's id (so metadata a later slice attaches
// to a tile survives a reslice); a genuinely new rect gets a fresh id that
// doesn't collide with any id being kept; an old tile with no matching rect
// simply doesn't appear in the result (implicitly removed).
std::vector<TileDefinition> reconcileTiles(const std::vector<TileDefinition>& oldTiles,
                                           const std::vector<TileDefinition>& newTiles);

} // namespace ArtCade::EditorNative
