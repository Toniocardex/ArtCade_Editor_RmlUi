#include "editor-native/app/tileset_editor_renderer.h"

#include "editor-native/app/checker_pattern.h"
#include "editor-native/model/tileset_slicing.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ArtCade::EditorNative {

namespace {

Rectangle destinationForTile(const TileDefinition& tile, const TextureResource& resource,
                             const Rectangle& sheetDest) {
    const float textureW = static_cast<float>(resource.texture.width);
    const float textureH = static_cast<float>(resource.texture.height);
    return Rectangle{
        sheetDest.x + (static_cast<float>(tile.x) / textureW) * sheetDest.width,
        sheetDest.y + (static_cast<float>(tile.y) / textureH) * sheetDest.height,
        (static_cast<float>(tile.width) / textureW) * sheetDest.width,
        (static_cast<float>(tile.height) / textureH) * sheetDest.height,
    };
}

void drawTilesetGrid(const TilesetEditorState& editorState, const TextureResource& resource,
                     const Rectangle& sheetDest, const ViewportRect& canvasRect,
                     const CanvasFont& canvasFont) {
    const std::vector<TileDefinition> tiles = tilesForSlicing(
        resource.texture.width, resource.texture.height, editorState.pendingSlicing);
    if (tiles.empty()) {
        drawCanvasText(canvasFont, "Tile size does not fit this sheet - lower it",
                       static_cast<float>(canvasRect.x) + 18.f,
                       static_cast<float>(canvasRect.y) + 18.f, 16.f,
                       Color{230, 176, 90, 230});
        return;
    }

    const int mouseX = GetMouseX();
    const int mouseY = GetMouseY();
    const bool mouseInCanvas = canvasRect.contains(mouseX, mouseY);
    for (const TileDefinition& tile : tiles) {
        const Rectangle cell = destinationForTile(tile, resource, sheetDest);
        const bool selected = editorState.selectedTileId && *editorState.selectedTileId == tile.id;
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
    }
    // Grid shape / tile size / zoom live in the RML toolbar and status bar
    // now - no raylib HUD text over the canvas.
}

} // namespace

Rectangle tilesetSheetDestination(const TextureResource& resource,
                                  const ViewportRect& canvasRect,
                                  const TilesetEditorState& editorState) {
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
    scale = std::clamp(scale, 0.25f, 16.f);
    scale *= clampTilesetEditorZoom(editorState.zoom);
    const float destW = textureW * scale;
    const float destH = textureH * scale;
    return Rectangle{
        area.x + (area.width - destW) * 0.5f + editorState.pan.x,
        area.y + (area.height - destH) * 0.5f + editorState.pan.y,
        destW,
        destH,
    };
}

void renderTilesetSelectedTileThumb(
    const AssetId& imageAssetId,
    const TileDefinition& tileDef,
    const ViewportRect& thumbRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
    if (!thumbRect.valid()) return;
    const TileDefinition* tile = &tileDef;
    if (tile->width <= 0 || tile->height <= 0) return;

    SceneFrameSprite requestSprite;
    requestSprite.assetId = imageAssetId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(imageAssetId);
    if (!resource || !resource->loaded) return;

    const Rectangle slot{
        static_cast<float>(thumbRect.x), static_cast<float>(thumbRect.y),
        static_cast<float>(thumbRect.width), static_cast<float>(thumbRect.height)};
    BeginScissorMode(thumbRect.x, thumbRect.y, thumbRect.width, thumbRect.height);
    drawTransparencyChecker(slot, slot);
    // Pixel-friendly: integer upscale when the tile fits its slot, plain fit
    // otherwise (the tile palette / timeline thumbnails' rule).
    float scale = std::min(slot.width / static_cast<float>(tile->width),
                           slot.height / static_cast<float>(tile->height));
    if (scale > 1.f) scale = std::floor(scale);
    if (scale > 0.f) {
        const float destW = static_cast<float>(tile->width) * scale;
        const float destH = static_cast<float>(tile->height) * scale;
        const Rectangle dest{
            slot.x + (slot.width - destW) * 0.5f,
            slot.y + (slot.height - destH) * 0.5f,
            destW, destH,
        };
        const Rectangle source{
            static_cast<float>(tile->x), static_cast<float>(tile->y),
            static_cast<float>(tile->width), static_cast<float>(tile->height),
        };
        DrawTexturePro(resource->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);
    }
    EndScissorMode();
}

void renderTilesetEditorCanvas(
    const TilesetAsset& asset,
    const TilesetEditorState& editorState,
    const ViewportRect& canvasRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests,
    const CanvasFont& canvasFont) {
    if (!canvasRect.valid()) return;
    BeginScissorMode(canvasRect.x, canvasRect.y, canvasRect.width, canvasRect.height);
    SceneFrameSprite requestSprite;
    requestSprite.assetId = asset.imageAssetId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(asset.imageAssetId);
    if (resource && resource->loaded) {
        const Rectangle source{
            0.f, 0.f,
            static_cast<float>(resource->texture.width),
            static_cast<float>(resource->texture.height),
        };
        const Rectangle dest = tilesetSheetDestination(*resource, canvasRect, editorState);
        const Rectangle canvasClip{
            static_cast<float>(canvasRect.x), static_cast<float>(canvasRect.y),
            static_cast<float>(canvasRect.width), static_cast<float>(canvasRect.height)};
        drawTransparencyChecker(dest, canvasClip);
        DrawTexturePro(resource->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);
        drawTilesetGrid(editorState, *resource, dest, canvasRect, canvasFont);
        DrawRectangleLinesEx(dest, 2.f, Color{120, 120, 130, 235});
    } else {
        drawCanvasText(canvasFont, "Missing source image",
                       static_cast<float>(canvasRect.x) + 18.f,
                       static_cast<float>(canvasRect.y) + 18.f, 18.f,
                       Color{230, 90, 120, 230});
    }
    EndScissorMode();
}

} // namespace ArtCade::EditorNative
