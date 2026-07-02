#include "editor-native/model/sprite_animation_slicing.h"

#include <algorithm>

namespace ArtCade::EditorNative {

int spriteAnimationSliceCellCount(int imageWidth, int imageHeight,
                                  const SpriteAnimationSliceGrid& grid) {
    if (imageWidth <= 0 || imageHeight <= 0) return 0;
    if (grid.frameWidth <= 0 || grid.frameHeight <= 0) return 0;
    if (grid.margin < 0 || grid.spacing < 0) return 0;
    const int usableW = imageWidth - grid.margin * 2;
    const int usableH = imageHeight - grid.margin * 2;
    if (usableW < grid.frameWidth || usableH < grid.frameHeight) return 0;
    const int stepX = grid.frameWidth + grid.spacing;
    const int stepY = grid.frameHeight + grid.spacing;
    if (stepX <= 0 || stepY <= 0) return 0;
    const int cols = 1 + (usableW - grid.frameWidth) / stepX;
    const int rows = 1 + (usableH - grid.frameHeight) / stepY;
    return cols * rows;
}

std::optional<SpriteAnimationFrameDef> spriteAnimationFrameForCell(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid, int cellIndex) {
    const int count = spriteAnimationSliceCellCount(imageWidth, imageHeight, grid);
    if (cellIndex < 0 || cellIndex >= count) return std::nullopt;
    const int usableW = imageWidth - grid.margin * 2;
    const int cols = 1 + (usableW - grid.frameWidth) / (grid.frameWidth + grid.spacing);
    const int col = cellIndex % cols;
    const int row = cellIndex / cols;
    const int x = grid.margin + col * (grid.frameWidth + grid.spacing);
    const int y = grid.margin + row * (grid.frameHeight + grid.spacing);
    return SpriteAnimationFrameDef{x, y, grid.frameWidth, grid.frameHeight};
}

std::optional<SpriteAnimationSliceGrid> spriteAnimationGridFromCellCounts(
    int imageWidth, int imageHeight, int columns, int rows, int margin, int spacing) {
    if (imageWidth <= 0 || imageHeight <= 0) return std::nullopt;
    if (columns <= 0 || rows <= 0 || margin < 0 || spacing < 0) return std::nullopt;
    const int usableW = imageWidth - margin * 2 - spacing * (columns - 1);
    const int usableH = imageHeight - margin * 2 - spacing * (rows - 1);
    if (usableW <= 0 || usableH <= 0) return std::nullopt;
    if (usableW % columns != 0 || usableH % rows != 0) return std::nullopt;
    SpriteAnimationSliceGrid grid;
    grid.frameWidth = usableW / columns;
    grid.frameHeight = usableH / rows;
    grid.margin = margin;
    grid.spacing = spacing;
    return grid;
}

std::vector<SpriteAnimationFrameDef> spriteAnimationFramesForGrid(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid) {
    std::vector<SpriteAnimationFrameDef> frames;
    const int count = spriteAnimationSliceCellCount(imageWidth, imageHeight, grid);
    frames.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) {
        if (const std::optional<SpriteAnimationFrameDef> frame =
                spriteAnimationFrameForCell(imageWidth, imageHeight, grid, i)) {
            frames.push_back(*frame);
        }
    }
    return frames;
}

std::vector<SpriteAnimationFrameDef> spriteAnimationFramesMatchingGrid(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid,
    const std::vector<SpriteAnimationFrameDef>& frames) {
    const std::vector<SpriteAnimationFrameDef> gridFrames =
        spriteAnimationFramesForGrid(imageWidth, imageHeight, grid);
    std::vector<SpriteAnimationFrameDef> filtered;
    filtered.reserve(frames.size());
    for (const SpriteAnimationFrameDef& frame : frames) {
        const bool validCell = std::find(gridFrames.begin(), gridFrames.end(), frame)
            != gridFrames.end();
        const bool duplicate = std::find(filtered.begin(), filtered.end(), frame)
            != filtered.end();
        if (validCell && !duplicate) filtered.push_back(frame);
    }
    return filtered;
}

} // namespace ArtCade::EditorNative
