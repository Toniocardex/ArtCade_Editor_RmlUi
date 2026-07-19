#include "editor-native/app/sprite_animation_canvas_input.h"

#include "editor-native/app/sprite_animation_preview_renderer.h"
#include "editor-native/commands/sprite_animation_commands.h"
#include "editor-native/model/sprite_animation_slicing.h"

#include <raylib.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

const SpriteAnimationClipDef* findAnimationClip(const SpriteAnimationAssetDef& asset,
                                                const std::optional<std::string>& clipId) {
    if (!clipId) return nullptr;
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (clip.id == *clipId) return &clip;
    }
    return nullptr;
}

const SpriteFrameDef* findPoolFrameByRect(const SpriteAnimationAssetDef& asset,
                                          const SpriteFrameDef& cell) {
    for (const SpriteFrameDef& frame : asset.frames) {
        if (frame.x == cell.x && frame.y == cell.y
            && frame.width == cell.width && frame.height == cell.height) {
            return &frame;
        }
    }
    return nullptr;
}

} // namespace

void routeSpriteAnimationCanvasInput(
    EditorCoordinator& coordinator,
    const ViewportRect& canvasRect,
    const RmlInputResult& rml,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
    if (rml.textFocus) return;
    if (!canvasRect.contains(GetMouseX(), GetMouseY())) return;

    const SpriteAnimationEditorState& editorState = coordinator.state().spriteAnimationEditor;
    if (!editorState.openAssetId) return;
    const SpriteAnimationAssetDef* asset =
        coordinator.document().findSpriteAnimationAsset(*editorState.openAssetId);
    if (!asset) return;

    const AssetId sheetId = editorSheetImageId(*asset, editorState.selectedClipId);
    SceneFrameSprite requestSprite;
    requestSprite.assetId = sheetId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(sheetId);
    if (!resource || !resource->loaded) return;

    const float mouseX = static_cast<float>(GetMouseX());
    const float mouseY = static_cast<float>(GetMouseY());

    // Zoom under the cursor: keep the texture point beneath the mouse fixed,
    // mirroring the Scene View gesture. Both rects come from the one shared
    // destination function, so the correction matches what is drawn.
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        const Rectangle before =
            spriteAnimationSheetDestination(*resource, canvasRect, editorState);
        const float scaleBefore =
            before.width / static_cast<float>(resource->texture.width);
        coordinator.apply(SetSpriteSheetZoomIntent{
            editorState.sheetZoom * (1.0f + wheel * 0.1f)});
        const SpriteAnimationEditorState& zoomed =
            coordinator.state().spriteAnimationEditor;
        const Rectangle after =
            spriteAnimationSheetDestination(*resource, canvasRect, zoomed);
        const float scaleAfter =
            after.width / static_cast<float>(resource->texture.width);
        const float u = (mouseX - before.x) / scaleBefore;
        const float v = (mouseY - before.y) / scaleBefore;
        coordinator.apply(PanSpriteSheetIntent{{
            mouseX - (after.x + u * scaleAfter),
            mouseY - (after.y + v * scaleAfter)}});
    }

    // Pan: middle-mouse, or Space + left-mouse (canvas pixels, like drawn).
    const bool spacePan = IsKeyDown(KEY_SPACE) && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    if (IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) || spacePan) {
        const Vector2 d = GetMouseDelta();
        if (d.x != 0.f || d.y != 0.f) {
            coordinator.apply(PanSpriteSheetIntent{{d.x, d.y}});
        }
        return;   // a pan gesture is not a cell toggle
    }

    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;
    const SpriteAnimationClipDef* clip = findAnimationClip(*asset, editorState.selectedClipId);
    if (!clip) return;

    const Rectangle dest =
        spriteAnimationSheetDestination(*resource, canvasRect,
                                        coordinator.state().spriteAnimationEditor);
    if (mouseX < dest.x || mouseX >= dest.x + dest.width
        || mouseY < dest.y || mouseY >= dest.y + dest.height) {
        return;
    }

    // Same derived grid the overlay draws: cell size from the frame counts.
    const std::optional<SpriteAnimationSliceGrid> derived =
        spriteAnimationGridFromCellCounts(
            resource->texture.width, resource->texture.height,
            editorState.sliceColumns, editorState.sliceRows,
            editorState.sliceMargin, editorState.sliceSpacing);
    if (!derived) return;
    const SpriteAnimationSliceGrid grid = *derived;
    const int cells =
        spriteAnimationSliceCellCount(resource->texture.width, resource->texture.height, grid);
    if (cells <= 0) return;
    const float scaleX = dest.width / static_cast<float>(resource->texture.width);
    const float scaleY = dest.height / static_cast<float>(resource->texture.height);
    const int sourceX = static_cast<int>((mouseX - dest.x) / scaleX);
    const int sourceY = static_cast<int>((mouseY - dest.y) / scaleY);
    int cell = -1;
    for (int candidate = 0; candidate < cells; ++candidate) {
        const std::optional<SpriteFrameDef> frame =
            spriteAnimationFrameForCell(resource->texture.width, resource->texture.height,
                                        grid, candidate);
        if (frame && sourceX >= frame->x && sourceX < frame->x + frame->width
            && sourceY >= frame->y && sourceY < frame->y + frame->height) {
            cell = candidate;
            break;
        }
    }
    if (cell < 0) return;
    const std::optional<SpriteFrameDef> cellFrame =
        spriteAnimationFrameForCell(resource->texture.width, resource->texture.height, grid, cell);
    if (!cellFrame) return;

    // Toggle against the clip's frameIds. The cell must already exist in the
    // asset frame pool (from Slice into Frames); otherwise there is nothing to
    // reference until the pool is rebuilt.
    const SpriteFrameDef* poolFrame = findPoolFrameByRect(*asset, *cellFrame);
    if (!poolFrame) return;

    coordinator.apply(ToggleAnimationSheetFrameSelectionIntent{poolFrame->id});

    // Shift-click only updates multi-select; plain click also toggles timeline
    // membership against the clip's frameIds.
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) return;

    std::vector<SpriteFrameId> frameIds = clip->frameIds;
    const auto existing = std::find(frameIds.begin(), frameIds.end(), poolFrame->id);
    if (existing == frameIds.end()) frameIds.push_back(poolFrame->id);
    else frameIds.erase(existing);

    coordinator.execute(SetAnimationClipFrameIdsCommand{
        asset->id, clip->id, std::move(frameIds)});
}

} // namespace ArtCade::EditorNative
