#include "editor-native/app/sprite_animation_preview_renderer.h"

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
    const SpriteAnimationSliceGrid grid{
        editorState.sliceFrameWidth,
        editorState.sliceFrameHeight,
        editorState.sliceMargin,
        editorState.sliceSpacing,
    };
    const int cells =
        spriteAnimationSliceCellCount(resource.texture.width, resource.texture.height, grid);
    const SpriteAnimationClipDef* clip = findAnimationClip(asset, editorState.selectedClipId);
    if (cells <= 0) {
        const std::string hint = "Frame " + std::to_string(grid.frameWidth) + "x"
            + std::to_string(grid.frameHeight) + " does not fit the "
            + std::to_string(resource.texture.width) + "x"
            + std::to_string(resource.texture.height)
            + " sheet - lower Frame W / Frame H";
        DrawText(hint.c_str(), canvasRect.x + 18, canvasRect.y + 18, 16,
                 Color{230, 176, 90, 230});
        return;
    }
    if (!clip) {
        DrawText("Add or select a clip, then click cells to toggle frames",
                 canvasRect.x + 18, canvasRect.y + 18, 16, Color{161, 161, 170, 220});
    }
    for (int i = 0; i < cells; ++i) {
        const std::optional<SpriteAnimationFrameDef> frame =
            spriteAnimationFrameForCell(resource.texture.width, resource.texture.height, grid, i);
        if (!frame) continue;
        const Rectangle cell = destinationForSourceFrame(*frame, resource, sheetDest);
        bool selected = false;
        if (clip) {
            selected = std::any_of(clip->frames.begin(), clip->frames.end(),
                                   [&](const SpriteAnimationFrameDef& candidate) {
                                       return candidate == *frame;
                                   });
        }
        if (selected) {
            DrawRectangleRec(cell, Color{59, 130, 246, 72});
            DrawRectangleLinesEx(cell, 2.f, Color{96, 165, 250, 255});
        } else {
            DrawRectangleLinesEx(cell, 1.f, Color{212, 212, 216, 110});
        }
        DrawText(std::to_string(i + 1).c_str(),
                 static_cast<int>(cell.x + 5.f), static_cast<int>(cell.y + 5.f),
                 12, selected ? Color{226, 240, 255, 255} : Color{82, 82, 91, 210});
    }
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
    float scale = std::min(area.width / textureW, area.height / textureH);
    if (scale > 1.f) scale = std::floor(scale);
    scale = std::clamp(scale, 0.25f, 10.f);
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
    SceneFrameSprite requestSprite;
    requestSprite.assetId = asset.imageId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(asset.imageId);
    if (resource && resource->loaded) {
        const Rectangle source{
            0.f, 0.f,
            static_cast<float>(resource->texture.width),
            static_cast<float>(resource->texture.height),
        };
        const Rectangle dest = spriteAnimationSheetDestination(*resource, canvasRect, editorState);
        DrawTexturePro(resource->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);
        DrawRectangleLinesEx(dest, 1.f, Color{63, 63, 70, 220});
        drawAnimationSheetGrid(asset, editorState, *resource, dest, canvasRect);
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
    const SpriteAnimationClipDef* clip = findAnimationClip(asset, editorState.selectedClipId);
    if (!clip || clip->frames.empty()) {
        DrawText("No frames", previewRect.x + 12, previewRect.y + 12, 14,
                 Color{82, 82, 91, 210});
        EndScissorMode();
        return;
    }
    SceneFrameSprite requestSprite;
    requestSprite.assetId = asset.imageId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(asset.imageId);
    if (!resource || !resource->loaded) {
        DrawText("Missing sprite sheet", previewRect.x + 12, previewRect.y + 12, 14,
                 Color{230, 90, 120, 230});
        EndScissorMode();
        return;
    }
    const SpriteAnimationSliceGrid grid{
        editorState.sliceFrameWidth,
        editorState.sliceFrameHeight,
        editorState.sliceMargin,
        editorState.sliceSpacing,
    };
    const std::vector<SpriteAnimationFrameDef> visibleFrames =
        spriteAnimationFramesMatchingGrid(
            resource->texture.width, resource->texture.height, grid, clip->frames);
    const std::vector<SpriteAnimationFrameDef>& previewFrames =
        visibleFrames.empty() ? clip->frames : visibleFrames;
    const std::size_t index =
        std::min(editorState.previewFrameIndex, previewFrames.size() - 1);
    const SpriteAnimationFrameDef& frame = previewFrames[index];
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
        std::to_string(index + 1) + " / " + std::to_string(previewFrames.size());
    DrawText(readout.c_str(), previewRect.x + 8,
             previewRect.y + previewRect.height - 20, 14, Color{161, 161, 170, 220});
    EndScissorMode();
}

} // namespace ArtCade::EditorNative
