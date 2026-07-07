#include "editor-native/app/sprite_animation_preview_renderer.h"

#include "editor-native/app/checker_pattern.h"
#include "editor-native/model/sprite_animation_slicing.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

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

Rectangle destinationForSourceFrame(const SpriteAnimationFrameDef& frame,
                                    const TextureResource& resource,
                                    const Rectangle& sheetDest) {
    const float textureW = static_cast<float>(resource.texture.width);
    const float textureH = static_cast<float>(resource.texture.height);
    return Rectangle{
        sheetDest.x + (static_cast<float>(frame.x) / textureW) * sheetDest.width,
        sheetDest.y + (static_cast<float>(frame.y) / textureH) * sheetDest.height,
        (static_cast<float>(frame.width) / textureW) * sheetDest.width,
        (static_cast<float>(frame.height) / textureH) * sheetDest.height,
    };
}

void drawAnimationSheetGrid(const SpriteAnimationAssetDef& asset,
                            const SpriteAnimationEditorState& editorState,
                            const TextureResource& resource,
                            const Rectangle& sheetDest,
                            const ViewportRect& canvasRect) {
    // Cell size is derived from the frame count and the sheet dimensions.
    const std::optional<SpriteAnimationSliceGrid> derived =
        spriteAnimationGridFromCellCounts(
            resource.texture.width, resource.texture.height,
            editorState.sliceColumns, editorState.sliceRows,
            editorState.sliceMargin, editorState.sliceSpacing);
    const SpriteAnimationClipDef* clip = findAnimationClip(asset, editorState.selectedClipId);
    if (!derived) {
        const std::string hint = std::to_string(editorState.sliceColumns) + " x "
            + std::to_string(editorState.sliceRows) + " frames do not fit the "
            + std::to_string(resource.texture.width) + "x"
            + std::to_string(resource.texture.height) + " sheet - lower the counts";
        DrawText(hint.c_str(), canvasRect.x + 18, canvasRect.y + 18, 16,
                 Color{230, 176, 90, 230});
        return;
    }
    const SpriteAnimationSliceGrid grid = *derived;
    const int cells =
        spriteAnimationSliceCellCount(resource.texture.width, resource.texture.height, grid);
    if (cells <= 0) return;
    if (!clip && !asset.clips.empty()) {
        DrawText("Select a clip, then click cells to add its frames",
                 canvasRect.x + 18, canvasRect.y + 18, 16, Color{161, 161, 170, 220});
    } else if (clip && clip->frames.empty()) {
        // Empty selected clip: tell the user how to fill THIS clip by hand, which
        // is how several animations (states) share one sheet - one clip each.
        DrawText("Click the cells for this clip - each clip is one animation",
                 canvasRect.x + 18, canvasRect.y + 18, 16, Color{130, 170, 240, 230});
    }

    const int mouseX = GetMouseX();
    const int mouseY = GetMouseY();
    const bool mouseInCanvas = canvasRect.contains(mouseX, mouseY);
    for (int i = 0; i < cells; ++i) {
        const std::optional<SpriteAnimationFrameDef> frame =
            spriteAnimationFrameForCell(resource.texture.width, resource.texture.height, grid, i);
        if (!frame) continue;
        const Rectangle cell = destinationForSourceFrame(*frame, resource, sheetDest);
        // Sequence position inside the clip (not the raw cell index): the badge
        // must tell the playback order at a glance.
        int order = -1;
        if (clip) {
            for (std::size_t f = 0; f < clip->frames.size(); ++f) {
                if (clip->frames[f] == *frame) { order = static_cast<int>(f); break; }
            }
        }
        const bool selected = order >= 0;
        const bool hovered = mouseInCanvas
            && static_cast<float>(mouseX) >= cell.x
            && static_cast<float>(mouseX) < cell.x + cell.width
            && static_cast<float>(mouseY) >= cell.y
            && static_cast<float>(mouseY) < cell.y + cell.height;
        if (selected) {
            DrawRectangleRec(cell, Color{59, 130, 246, 72});
            DrawRectangleLinesEx(cell, 2.f, Color{96, 165, 250, 255});
        } else if (hovered) {
            DrawRectangleRec(cell, Color{212, 212, 216, 26});
            DrawRectangleLinesEx(cell, 1.5f, Color{212, 212, 216, 220});
        } else {
            DrawRectangleLinesEx(cell, 1.f, Color{212, 212, 216, 110});
        }
        if (selected) {
            const std::string label = std::to_string(order + 1);
            const int textW = MeasureText(label.c_str(), 12);
            DrawRectangle(static_cast<int>(cell.x) + 3, static_cast<int>(cell.y) + 3,
                          textW + 8, 16, Color{59, 130, 246, 255});
            DrawText(label.c_str(), static_cast<int>(cell.x) + 7,
                     static_cast<int>(cell.y) + 5, 12, Color{255, 255, 255, 255});
        } else {
            DrawText(std::to_string(i + 1).c_str(),
                     static_cast<int>(cell.x + 5.f), static_cast<int>(cell.y + 5.f),
                     12, hovered ? Color{212, 212, 216, 240} : Color{82, 82, 91, 210});
        }
    }

    // HUD: frame count, cell size and effective zoom, bottom-left of the canvas.
    const int zoomPct = static_cast<int>(
        sheetDest.width / static_cast<float>(resource.texture.width) * 100.f + 0.5f);
    const std::string hud = std::to_string(editorState.sliceColumns) + " x "
        + std::to_string(editorState.sliceRows) + " frames   "
        + std::to_string(grid.frameWidth) + " x " + std::to_string(grid.frameHeight)
        + " px cell   " + std::to_string(zoomPct) + "%";
    DrawText(hud.c_str(), canvasRect.x + 12,
             canvasRect.y + canvasRect.height - 24, 14, Color{161, 161, 170, 230});
}

} // namespace

Rectangle spriteAnimationSheetDestination(const TextureResource& resource,
                                          const ViewportRect& canvasRect,
                                          const SpriteAnimationEditorState& editorState) {
    Rectangle area{
        static_cast<float>(canvasRect.x),
        static_cast<float>(canvasRect.y),
        std::max(80.f, static_cast<float>(canvasRect.width)),
        std::max(80.f, static_cast<float>(canvasRect.height)),
    };
    const float textureW = static_cast<float>(resource.texture.width);
    const float textureH = static_cast<float>(resource.texture.height);
    // Fit the sheet to the canvas (integer upscale for crisp pixels). The ceiling
    // only guards against a tiny sprite filling the whole canvas; the fit min()
    // already prevents overflow, so a small pixel sheet still opens readable.
    float scale = std::min(area.width / textureW, area.height / textureH);
    if (scale > 1.f) scale = std::floor(scale);
    scale = std::clamp(scale, 0.25f, 16.f);
    scale *= clampSheetZoom(editorState.sheetZoom);
    const float destW = textureW * scale;
    const float destH = textureH * scale;
    return Rectangle{
        area.x + (area.width - destW) * 0.5f + editorState.sheetPan.x,
        area.y + (area.height - destH) * 0.5f + editorState.sheetPan.y,
        destW,
        destH,
    };
}

void renderSpriteAnimationPreview(
    const SpriteAnimationAssetDef& asset,
    const SpriteAnimationEditorState& editorState,
    const ViewportRect& canvasRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
    if (!canvasRect.valid()) return;
    BeginScissorMode(canvasRect.x, canvasRect.y, canvasRect.width, canvasRect.height);
    const AssetId sheetId = editorSheetImageId(asset, editorState.selectedClipId);
    SceneFrameSprite requestSprite;
    requestSprite.assetId = sheetId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(sheetId);
    if (resource && resource->loaded) {
        const Rectangle source{
            0.f, 0.f,
            static_cast<float>(resource->texture.width),
            static_cast<float>(resource->texture.height),
        };
        const Rectangle dest = spriteAnimationSheetDestination(*resource, canvasRect, editorState);
        const Rectangle canvasClip{
            static_cast<float>(canvasRect.x), static_cast<float>(canvasRect.y),
            static_cast<float>(canvasRect.width), static_cast<float>(canvasRect.height)};
        drawTransparencyChecker(dest, canvasClip);
        DrawTexturePro(resource->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);
        drawAnimationSheetGrid(asset, editorState, *resource, dest, canvasRect);
        // Image bounds: a brighter 2px frame, distinct from the 1px cell lines.
        DrawRectangleLinesEx(dest, 2.f, Color{120, 120, 130, 235});
    } else {
        DrawText("Missing sprite sheet", canvasRect.x + 18, canvasRect.y + 18, 18,
                 Color{230, 90, 120, 230});
    }
    EndScissorMode();
}

void renderSpriteAnimationClipPreview(
    const SpriteAnimationAssetDef& asset,
    const SpriteAnimationEditorState& editorState,
    const ViewportRect& previewRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
    if (!previewRect.valid()) return;
    BeginScissorMode(previewRect.x, previewRect.y, previewRect.width, previewRect.height);
    const Rectangle previewArea{
        static_cast<float>(previewRect.x), static_cast<float>(previewRect.y),
        static_cast<float>(previewRect.width), static_cast<float>(previewRect.height)};
    drawTransparencyChecker(previewArea, previewArea);
    const SpriteAnimationClipDef* clip = findAnimationClip(asset, editorState.selectedClipId);
    if (!clip || clip->frames.empty()) {
        DrawText("No frames", previewRect.x + 12, previewRect.y + 12, 14,
                 Color{82, 82, 91, 210});
        EndScissorMode();
        return;
    }
    SceneFrameSprite requestSprite;
    requestSprite.assetId = clip->imageId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(clip->imageId);
    if (!resource || !resource->loaded) {
        DrawText("Missing sprite sheet", previewRect.x + 12, previewRect.y + 12, 14,
                 Color{230, 90, 120, 230});
        EndScissorMode();
        return;
    }
    // The clip's frames are the sequence: play them straight, in order.
    const std::size_t index =
        std::min(editorState.previewFrameIndex, clip->frames.size() - 1);
    const SpriteAnimationFrameDef& frame = clip->frames[index];
    if (frame.width > 0 && frame.height > 0) {
        // Pixel-friendly: integer upscale when the frame fits, plain fit otherwise.
        float scale = std::min(
            static_cast<float>(previewRect.width)  / static_cast<float>(frame.width),
            static_cast<float>(previewRect.height) / static_cast<float>(frame.height));
        if (scale > 1.f) scale = std::floor(scale);
        const Rectangle source{
            static_cast<float>(frame.x), static_cast<float>(frame.y),
            static_cast<float>(frame.width), static_cast<float>(frame.height),
        };
        const Rectangle dest{
            previewRect.x + (previewRect.width  - frame.width  * scale) * 0.5f,
            previewRect.y + (previewRect.height - frame.height * scale) * 0.5f,
            frame.width * scale,
            frame.height * scale,
        };
        DrawTexturePro(resource->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);
    }
    const std::string readout =
        std::to_string(index + 1) + " / " + std::to_string(clip->frames.size());
    DrawText(readout.c_str(), previewRect.x + 8,
             previewRect.y + previewRect.height - 20, 14, Color{161, 161, 170, 220});
    EndScissorMode();
}

void renderSpriteAnimationTimelineThumbnails(
    const SpriteAnimationAssetDef& asset,
    const SpriteAnimationClipDef& clip,
    const std::vector<ViewportRect>& thumbRects,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
    if (thumbRects.empty() || clip.frames.empty()) return;
    SceneFrameSprite requestSprite;
    requestSprite.assetId = clip.imageId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(clip.imageId);
    if (!resource || !resource->loaded) return;

    const std::size_t count = std::min(thumbRects.size(), clip.frames.size());
    for (std::size_t i = 0; i < count; ++i) {
        const ViewportRect& slot = thumbRects[i];
        if (!slot.valid()) continue;
        const SpriteAnimationFrameDef& frame = clip.frames[i];
        if (frame.width <= 0 || frame.height <= 0) continue;
        BeginScissorMode(slot.x, slot.y, slot.width, slot.height);
        const Rectangle slotArea{
            static_cast<float>(slot.x), static_cast<float>(slot.y),
            static_cast<float>(slot.width), static_cast<float>(slot.height)};
        drawTransparencyChecker(slotArea, slotArea);
        // Pixel-friendly: integer upscale when the frame fits, plain fit otherwise.
        float scale = std::min(
            static_cast<float>(slot.width)  / static_cast<float>(frame.width),
            static_cast<float>(slot.height) / static_cast<float>(frame.height));
        if (scale > 1.f) scale = std::floor(scale);
        if (scale <= 0.f) { EndScissorMode(); continue; }
        const Rectangle source{
            static_cast<float>(frame.x), static_cast<float>(frame.y),
            static_cast<float>(frame.width), static_cast<float>(frame.height)};
        const Rectangle dest{
            slot.x + (slot.width  - frame.width  * scale) * 0.5f,
            slot.y + (slot.height - frame.height * scale) * 0.5f,
            frame.width * scale,
            frame.height * scale};
        DrawTexturePro(resource->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);
        EndScissorMode();
    }
}

} // namespace ArtCade::EditorNative
