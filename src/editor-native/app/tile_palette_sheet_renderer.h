#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/model/tile_palette_projection.h"
#include "editor-native/view/canvas_font.h"
#include "editor-native/view/scene_view_camera.h"
#include "editor-native/view/texture_cache.h"
#include "editor-native/view/tileset_empty_tiles.h"

#include <raylib.h>

namespace ArtCade::EditorNative {

// In-progress marquee selection on the palette sheet, in grid-cell
// coordinates (inclusive corners). App-side gesture state (see
// tile_palette_input.h): drawn as a preview, distinct from the committed
// stamp highlight.
struct TilePaletteMarquee {
    bool active = false;
    int col0 = 0;
    int row0 = 0;
    int col1 = 0;
    int row1 = 0;
};

// Canonical projection for this hole + view (shared by render and input).
TilePaletteViewportProjection tilePaletteProjectionForHole(
    const TextureResource& texture,
    const ViewportRect& holeRect,
    const TilePaletteViewState& view);

// Convenience: sheet destination rectangle from the shared projection.
Rectangle tilePaletteSheetDestination(const TextureResource& texture,
                                      const ViewportRect& holeRect,
                                      const TilePaletteViewState& view,
                                      int tileWidthPx = 0, int tileHeightPx = 0);

// Draws the palette sheet from one projection: checker, texture, empties,
// adaptive grid, stamp, marquee, scrollbars, and loading/error overlays.
// Presentation only — never mutates ProjectDocument or workspace stamp state.
// `canvasFont` may be null (status text falls back to raylib's default font).
void renderTilePaletteSheet(const TilesetAsset& tileset,
                            const TilemapEditorState& tilemapEditor,
                            const ViewportRect& holeRect,
                            const ViewportRect& clipRect,
                            const TextureResource* texture,
                            const TilesetEmptyMaskView& emptyMask,
                            const TilePaletteMarquee& marquee,
                            const CanvasFont* canvasFont = nullptr);

} // namespace ArtCade::EditorNative
