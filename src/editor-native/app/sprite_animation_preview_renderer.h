#pragma once

#include "core/types.h"
#include "editor-native/model/editor_state.h"
#include "editor-native/view/scene_view.h"
#include "editor-native/view/texture_cache.h"

#include <raylib.h>

#include <unordered_map>

namespace ArtCade::EditorNative {

// The single sheet->canvas mapping (fit scale x sheetZoom, centred + sheetPan),
// shared by the renderer and the canvas input so a click lands on exactly what
// is drawn — the sprite-sheet analogue of makeSceneViewCamera().
Rectangle spriteAnimationSheetDestination(const TextureResource& resource,
                                          const ViewportRect& canvasRect,
                                          const SpriteAnimationEditorState& editorState);

void renderSpriteAnimationPreview(
    const SpriteAnimationAssetDef& asset,
    const SpriteAnimationEditorState& editorState,
    const ViewportRect& canvasRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests);

// Clip preview canvas: draws the workspace playhead's current frame at a
// pixel-friendly integer scale. Read-only over document + workspace state.
void renderSpriteAnimationClipPreview(
    const SpriteAnimationAssetDef& asset,
    const SpriteAnimationEditorState& editorState,
    const ViewportRect& previewRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests);

} // namespace ArtCade::EditorNative
