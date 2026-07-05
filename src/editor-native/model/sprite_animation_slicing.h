#pragma once

#include "core/types.h"

#include <optional>
#include <vector>

namespace ArtCade::EditorNative {

struct SpriteAnimationSliceGrid {
    int frameWidth = 32;
    int frameHeight = 32;
    int margin = 0;
    int spacing = 0;
};

int spriteAnimationSliceCellCount(int imageWidth, int imageHeight,
                                  const SpriteAnimationSliceGrid& grid);

std::optional<SpriteAnimationFrameDef> spriteAnimationFrameForCell(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid, int cellIndex);

// Derives the pixel cell size from a frame count (columns x rows). This is the
// primary slicing input: the user says how many frames, the sheet dimensions
// give the cell size. Integer division floors the cell so a sheet that does not
// divide evenly still yields frames (the remainder strip is left uncovered);
// returns nullopt only when the counts cannot produce a >= 1px cell.
std::optional<SpriteAnimationSliceGrid> spriteAnimationGridFromCellCounts(
    int imageWidth, int imageHeight, int columns, int rows, int margin, int spacing);

std::vector<SpriteAnimationFrameDef> spriteAnimationFramesForGrid(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid);

} // namespace ArtCade::EditorNative
