#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
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

// Where the whole sheet lands inside the palette hole: fit-to-hole scale
// (integer upscale floored, pixel-crisp), multiplied by the per-tileset view
// zoom, centered, then offset by the view pan. Mirrors tilesetSheetDestination
// so the two sheet canvases behave identically; input hit-testing uses this
// exact mapping so a click lands on what is drawn.
Rectangle tilePaletteSheetDestination(const TextureResource& texture,
                                      const ViewportRect& holeRect,
                                      const TilePaletteViewState& view);

// Draws the palette sheet: checker, the sheet texture once, dimmed empty
// tiles, the per-tile grid, the committed stamp's N x M region (border +
// per-slot fill, holes hatched), the hover highlight and the live marquee.
//
// Presentation only, over already-resolved data: the caller (EditorApp) owns
// texture preparation and mask resolution - this function never touches the
// TextureCache, the ProjectDocument, or the filesystem. `texture` may be
// null/unloaded (nothing is drawn but the hole background).
void renderTilePaletteSheet(const TilesetAsset& tileset,
                            const TilemapEditorState& tilemapEditor,
                            const ViewportRect& holeRect,
                            const ViewportRect& clipRect,
                            const TextureResource* texture,
                            const TilesetEmptyMaskView& emptyMask,
                            const TilePaletteMarquee& marquee);

} // namespace ArtCade::EditorNative
