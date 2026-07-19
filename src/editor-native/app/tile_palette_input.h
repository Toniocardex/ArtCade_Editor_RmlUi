#pragma once

#include "core/types.h"
#include "editor-native/app/editor_input.h"
#include "editor-native/app/tile_palette_sheet_renderer.h"
#include "editor-native/model/tile_stamp.h"
#include "editor-native/view/texture_cache.h"
#include "editor-native/view/tileset_empty_tiles.h"

#include <optional>

#include <raylib.h>

namespace ArtCade::EditorNative {

class EditorCoordinator;

// App-side gesture state for the Tile Palette sheet (mirrors ViewportDrag:
// the single authority of the not-yet-committed gesture, never workspace or
// document state). The marquee is cancelled - never committed - on Escape,
// focus loss, workspace/entity/tileset changes, Play, or the hole vanishing;
// the caller invokes cancelTilePaletteGesture() whenever it stops routing.
struct TilePaletteInputState {
    TilePaletteMarquee marquee;
    AssetId            marqueeTilesetAssetId;   // tileset the marquee started on
    double             lastClickTime = 0.0;     // app-side double-click detection
    Vector2            lastClickPos{};
    // Change detection for pan-selected-into-view (Picker sync).
    std::optional<TilemapTileStamp> lastPannedStamp;
};

inline void cancelTilePaletteGesture(TilePaletteInputState& state) {
    state.marquee.active = false;
}

// Routes one frame of mouse input over the palette hole: Ctrl+wheel =
// cursor-anchored zoom (plain wheel stays RmlUi's - the Inspector scrolls),
// middle-mouse or Space+left = pan, left drag = marquee committed as a
// SelectPaintStampIntent on release (blocked while the empty mask is not
// conclusive - Ready selects with holes, Failed selects unfiltered),
// double-click = OpenTilesetEditorIntent. The hole element carries no RmlUi
// data-action, so this router is the only click handler - no double-fire.
//
// Also pans the committed stamp's whole source region into view when the
// selection changed from outside the palette (the scene Picker).
void routeTilePaletteInput(EditorCoordinator& coordinator,
                           const TilesetAsset& tileset,
                           const ViewportRect& holeRect,
                           const ViewportRect& clipRect,
                           const RmlInputResult& rml,
                           const TilesetEmptyMaskView& emptyMask,
                           const TextureResource* texture,
                           TilePaletteInputState& state);

} // namespace ArtCade::EditorNative
