#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/view/scene_view.h"
#include "editor-native/view/texture_cache.h"

#include <raylib.h>

#include <unordered_map>

namespace ArtCade::EditorNative {

// The single sheet->canvas mapping (fit scale x zoom, centred + pan), shared
// by the renderer and the canvas input so a click lands on exactly what is
// drawn - the tileset analogue of spriteAnimationSheetDestination().
Rectangle tilesetSheetDestination(const TextureResource& resource,
                                  const ViewportRect& canvasRect,
                                  const TilesetEditorState& editorState);

// Draws the source image plus a live grid overlay computed from the pending
// (not yet applied) slicing config - this is the preview; ProjectDocument's
// committed asset->tiles is never read here.
void renderTilesetEditorCanvas(
    const TilesetAsset& asset,
    const TilesetEditorState& editorState,
    const ViewportRect& canvasRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests);

// Paints one tile's crop into the settings panel's Selected Tile slot (RML
// lays the box out; raylib fills it after host.render(), the same pattern as
// the tile palette / timeline thumbnails). The caller resolves the selected
// id to a TileDefinition first - committed tiles when the id matches, else
// the live pending grid - because selection ids come from the pending grid.
void renderTilesetSelectedTileThumb(
    const AssetId& imageAssetId,
    const TileDefinition& tile,
    const ViewportRect& thumbRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests);

} // namespace ArtCade::EditorNative
