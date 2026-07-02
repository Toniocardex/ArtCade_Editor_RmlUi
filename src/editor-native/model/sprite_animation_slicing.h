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

std::optional<SpriteAnimationSliceGrid> spriteAnimationGridFromCellCounts(
    int imageWidth, int imageHeight, int columns, int rows, int margin, int spacing);

std::vector<SpriteAnimationFrameDef> spriteAnimationFramesForGrid(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid);

std::vector<SpriteAnimationFrameDef> spriteAnimationFramesMatchingGrid(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid,
    const std::vector<SpriteAnimationFrameDef>& frames);

} // namespace ArtCade::EditorNative
