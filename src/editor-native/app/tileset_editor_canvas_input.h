#pragma once

#include "editor-native/app/editor_input.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/view/scene_view.h"
#include "editor-native/view/texture_cache.h"

#include <unordered_map>

namespace ArtCade::EditorNative {

void routeTilesetEditorCanvasInput(
    EditorCoordinator& coordinator,
    const ViewportRect& canvasRect,
    const RmlInputResult& rml,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests);

} // namespace ArtCade::EditorNative
