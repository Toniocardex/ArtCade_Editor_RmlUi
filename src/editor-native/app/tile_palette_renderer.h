#pragma once

#include "core/types.h"
#include "editor-native/view/scene_view.h"
#include "editor-native/view/texture_cache.h"

#include <raylib.h>

#include <optional>
#include <unordered_map>
#include <vector>

namespace ArtCade::EditorNative {

// Draws each tile's cropped thumbnail into its matching per-thumb slot rect -
// the tile-palette analogue of renderSpriteAnimationTimelineThumbnails.
// thumbRects is parallel to tileset.tiles (index i -> tile i); an invalid
// (not yet laid out) rect is skipped, so a thumb not yet in the DOM just
// waits a frame. One shared texture, N crops - never a texture per tile.
// Selected/hovered borders mirror the Tileset Editor canvas' own grid cells
// so a tile reads the same way in both places.
//
// `clipRect` is the Inspector's own visible content rect (its RmlUi element
// has overflow-y: auto, but that clip applies only to RmlUi's own draw calls -
// this raylib overlay pass is drawn afterward and knows nothing about it on
// its own). A thumb slot is intersected with clipRect before drawing, and
// skipped entirely if the intersection is empty, so a palette row scrolled
// toward the bottom of the panel is cut off at the panel's own edge instead
// of painting over the Console below it.
void renderTilePalette(
    const TilesetAsset& tileset,
    const std::optional<TileId>& selectedTileId,
    const std::vector<ViewportRect>& thumbRects,
    const ViewportRect& clipRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests);

} // namespace ArtCade::EditorNative
