#include "editor-native/model/sprite_animation_slicing.h"

#include <algorithm>
#include <string>

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

std::optional<SpriteFrameDef> spriteAnimationFrameForCell(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid, int cellIndex) {
    const int count = spriteAnimationSliceCellCount(imageWidth, imageHeight, grid);
    if (cellIndex < 0 || cellIndex >= count) return std::nullopt;
    const int usableW = imageWidth - grid.margin * 2;
    const int cols = 1 + (usableW - grid.frameWidth) / (grid.frameWidth + grid.spacing);
    const int col = cellIndex % cols;
    const int row = cellIndex / cols;
    const int x = grid.margin + col * (grid.frameWidth + grid.spacing);
    const int y = grid.margin + row * (grid.frameHeight + grid.spacing);
    SpriteFrameDef frame;
    frame.id = "frame-" + std::to_string(cellIndex + 1);
    frame.x = x;
    frame.y = y;
    frame.width = grid.frameWidth;
    frame.height = grid.frameHeight;
    return frame;
}

std::optional<SpriteAnimationSliceGrid> spriteAnimationGridFromCellCounts(
    int imageWidth, int imageHeight, int columns, int rows, int margin, int spacing) {
    if (imageWidth <= 0 || imageHeight <= 0) return std::nullopt;
    if (columns <= 0 || rows <= 0 || margin < 0 || spacing < 0) return std::nullopt;
    const int usableW = imageWidth - margin * 2 - spacing * (columns - 1);
    const int usableH = imageHeight - margin * 2 - spacing * (rows - 1);
    // Reject only when a cell cannot be at least 1px; otherwise floor so an
    // imperfect division still slices (no "does not fit" dead end for the user).
    if (usableW < columns || usableH < rows) return std::nullopt;
    SpriteAnimationSliceGrid grid;
    grid.frameWidth = usableW / columns;
    grid.frameHeight = usableH / rows;
    grid.margin = margin;
    grid.spacing = spacing;
    return grid;
}

std::vector<SpriteFrameDef> spriteAnimationFramesForGrid(
    int imageWidth, int imageHeight, const SpriteAnimationSliceGrid& grid) {
    std::vector<SpriteFrameDef> frames;
    const int count = spriteAnimationSliceCellCount(imageWidth, imageHeight, grid);
    frames.reserve(static_cast<std::size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) {
        if (const std::optional<SpriteFrameDef> frame =
                spriteAnimationFrameForCell(imageWidth, imageHeight, grid, i)) {
            frames.push_back(*frame);
        }
    }
    return frames;
}

} // namespace ArtCade::EditorNative
