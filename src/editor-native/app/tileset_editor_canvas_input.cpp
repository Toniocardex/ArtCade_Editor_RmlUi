#include "editor-native/app/tileset_editor_canvas_input.h"

#include "editor-native/app/tileset_editor_renderer.h"
#include "editor-native/model/tileset_slicing.h"

#include <raylib.h>

namespace ArtCade::EditorNative {

void routeTilesetEditorCanvasInput(
    EditorCoordinator& coordinator,
    const ViewportRect& canvasRect,
    const RmlInputResult& rml,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
    if (rml.textFocus) return;
    if (!canvasRect.contains(GetMouseX(), GetMouseY())) return;

    const TilesetEditorState& editorState = coordinator.state().tilesetEditor;
    if (!editorState.openAssetId) return;
    const TilesetAsset* asset = coordinator.document().findTilesetAsset(*editorState.openAssetId);
    if (!asset) return;

    SceneFrameSprite requestSprite;
    requestSprite.assetId = asset->imageAssetId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(asset->imageAssetId);
    if (!resource || !resource->loaded) return;

    const float mouseX = static_cast<float>(GetMouseX());
    const float mouseY = static_cast<float>(GetMouseY());

    // Zoom under the cursor: keep the texture point beneath the mouse fixed,
    // mirroring the Sprite Animation Editor's identical gesture.
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        const Rectangle before = tilesetSheetDestination(*resource, canvasRect, editorState);
        const float scaleBefore = before.width / static_cast<float>(resource->texture.width);
        coordinator.apply(SetTilesetEditorZoomIntent{editorState.zoom * (1.0f + wheel * 0.1f)});
        const TilesetEditorState& zoomed = coordinator.state().tilesetEditor;
        const Rectangle after = tilesetSheetDestination(*resource, canvasRect, zoomed);
        const float scaleAfter = after.width / static_cast<float>(resource->texture.width);
        const float u = (mouseX - before.x) / scaleBefore;
        const float v = (mouseY - before.y) / scaleBefore;
        coordinator.apply(PanTilesetEditorIntent{{
            mouseX - (after.x + u * scaleAfter),
            mouseY - (after.y + v * scaleAfter)}});
    }

    // Pan: middle-mouse, or Space + left-mouse (canvas pixels, like drawn).
    const bool spacePan = IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || spacePan) {
        const Vector2 d = GetMouseDelta();
        if (d.x != 0.f || d.y != 0.f) {
            coordinator.apply(PanTilesetEditorIntent{{d.x, d.y}});
        }
        return;   // a pan gesture is not a tile click
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    const Rectangle dest =
        tilesetSheetDestination(*resource, canvasRect, coordinator.state().tilesetEditor);
    if (mouseX < dest.x || mouseX >= dest.x + dest.width
        || mouseY < dest.y || mouseY >= dest.y + dest.height) {
        return;
    }

    // Same live-preview grid the overlay draws (from pending slicing, not the
    // asset's committed tiles).
    const std::vector<TileDefinition> tiles = tilesForSlicing(
        resource->texture.width, resource->texture.height, editorState.pendingSlicing);
    const float scaleX = dest.width / static_cast<float>(resource->texture.width);
    const float scaleY = dest.height / static_cast<float>(resource->texture.height);
    const int sourceX = static_cast<int>((mouseX - dest.x) / scaleX);
    const int sourceY = static_cast<int>((mouseY - dest.y) / scaleY);
    for (const TileDefinition& tile : tiles) {
        if (sourceX >= tile.x && sourceX < tile.x + tile.width
            && sourceY >= tile.y && sourceY < tile.y + tile.height) {
            coordinator.apply(SelectTilesetTileIntent{tile.id});
            break;
        }
    }
}

} // namespace ArtCade::EditorNative
