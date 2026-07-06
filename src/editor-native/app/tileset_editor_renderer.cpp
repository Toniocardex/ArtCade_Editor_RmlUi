#include "editor-native/app/tileset_editor_renderer.h"

#include "editor-native/model/tileset_slicing.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ArtCade::EditorNative {

namespace {

// Transparency checker under the sheet: empty pixels must read as empty, not
// as the canvas background. Mirrors sprite_animation_preview_renderer.cpp's
// drawTransparencyChecker exactly.
void drawTransparencyChecker(const Rectangle& area, const Rectangle& clip) {
    constexpr int kTile = 8;
    const int x0 = static_cast<int>(std::floor(std::max(area.x, clip.x)));
    const int y0 = static_cast<int>(std::floor(std::max(area.y, clip.y)));
    const int x1 = static_cast<int>(std::ceil(std::min(area.x + area.width, clip.x + clip.width)));
    const int y1 = static_cast<int>(std::ceil(std::min(area.y + area.height, clip.y + clip.height)));
    for (int y = y0; y < y1; y += kTile) {
        for (int x = x0; x < x1; x += kTile) {
            const bool dark = (((x - x0) / kTile) + ((y - y0) / kTile)) % 2 == 0;
            DrawRectangle(x, y, std::min(kTile, x1 - x), std::min(kTile, y1 - y),
                          dark ? Color{13, 13, 15, 255} : Color{22, 22, 26, 255});
        }
    }
}

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
                     const Rectangle& sheetDest, const ViewportRect& canvasRect) {
    const std::vector<TileDefinition> tiles = tilesForSlicing(
        resource.texture.width, resource.texture.height, editorState.pendingSlicing);
    if (tiles.empty()) {
        DrawText("Tile size does not fit this sheet - lower it",
                 canvasRect.x + 18, canvasRect.y + 18, 16, Color{230, 176, 90, 230});
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

    // HUD: grid shape, tile size and effective zoom, bottom-left of the canvas.
    const TilesetSliceResult slice = computeTilesetSlicing(
        resource.texture.width, resource.texture.height, editorState.pendingSlicing);
    const int zoomPct = static_cast<int>(
        sheetDest.width / static_cast<float>(resource.texture.width) * 100.f + 0.5f);
    const std::string hud = std::to_string(slice.columns) + " x " + std::to_string(slice.rows)
        + " tiles   " + std::to_string(editorState.pendingSlicing.tileWidth) + " x "
        + std::to_string(editorState.pendingSlicing.tileHeight) + " px tile   "
        + std::to_string(zoomPct) + "%";
    DrawText(hud.c_str(), canvasRect.x + 12,
             canvasRect.y + canvasRect.height - 24, 14, Color{161, 161, 170, 230});
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

void renderTilesetEditorCanvas(
    const TilesetAsset& asset,
    const TilesetEditorState& editorState,
    const ViewportRect& canvasRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
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
        drawTilesetGrid(editorState, *resource, dest, canvasRect);
        DrawRectangleLinesEx(dest, 2.f, Color{120, 120, 130, 235});
    } else {
        DrawText("Missing source image", canvasRect.x + 18, canvasRect.y + 18, 18,
                 Color{230, 90, 120, 230});
    }
    EndScissorMode();
}

} // namespace ArtCade::EditorNative
