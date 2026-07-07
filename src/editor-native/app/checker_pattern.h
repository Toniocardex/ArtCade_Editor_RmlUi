#pragma once

#include <raylib.h>

namespace ArtCade::EditorNative {

// Transparency checker under a texture: empty pixels must read as empty, not
// as the canvas background. Shared by every canvas that can show a partially
// transparent source image (Sprite Animation Editor, Tileset Editor, Tile
// Palette) so the pattern (tile size, colors) stays visually identical
// wherever it appears, rather than N copies of the same drawing code.
void drawTransparencyChecker(const Rectangle& area, const Rectangle& clip);

} // namespace ArtCade::EditorNative
