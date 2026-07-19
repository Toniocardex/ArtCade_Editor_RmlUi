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

struct TilePaletteInputState {
    TilePaletteMarquee marquee;
    AssetId            marqueeTilesetAssetId;
    double             lastClickTime = 0.0;
    Vector2            lastClickPos{};
    std::optional<TilemapTileStamp> lastPannedStamp;

    // Scrollbar thumb drag (projection of scrollOffset — not a second authority).
    enum class ScrollDrag { None, Horizontal, Vertical };
    ScrollDrag scrollDrag = ScrollDrag::None;
    float      scrollDragGrab = 0.f;   // mouse pos along axis at press
    float      scrollDragOrigin = 0.f; // scrollOffset component at press
};

inline void cancelTilePaletteGesture(TilePaletteInputState& state) {
    state.marquee.active = false;
    state.scrollDrag = TilePaletteInputState::ScrollDrag::None;
}

// Routes one frame of mouse input over the palette hole:
//   wheel           → vertical scroll
//   Shift+wheel     → horizontal scroll
//   Ctrl+wheel      → integer zoom step, cursor-anchored
//   middle / Space+LMB → pan
//   scrollbar thumb → drag scroll
//   scrollbar track → jump scroll (never marquee)
//   left drag       → marquee → SelectPaintStampIntent
//   double-click    → OpenTilesetEditorIntent
// Also applies first-open init and pending Fit requests (baked once; never
// re-fit on resize).
void routeTilePaletteInput(EditorCoordinator& coordinator,
                           const TilesetAsset& tileset,
                           const ViewportRect& holeRect,
                           const ViewportRect& clipRect,
                           const RmlInputResult& rml,
                           const TilesetEmptyMaskView& emptyMask,
                           const TextureResource* texture,
                           TilePaletteInputState& state);

} // namespace ArtCade::EditorNative
