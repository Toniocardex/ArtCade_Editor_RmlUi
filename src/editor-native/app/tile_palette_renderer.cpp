#include "editor-native/app/tile_palette_renderer.h"

#include "editor-native/app/checker_pattern.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorNative {

void renderTilePalette(
    const TilesetAsset& tileset,
    const std::optional<TileId>& selectedTileId,
    const std::vector<ViewportRect>& thumbRects,
    const ViewportRect& clipRect,
    TextureCache& textureCache,
    const std::unordered_map<AssetId, TextureRequest>& requests) {
    if (thumbRects.empty() || tileset.tiles.empty() || !clipRect.valid()) return;
    SceneFrameSprite requestSprite;
    requestSprite.assetId = tileset.imageAssetId;
    requestSprite.visible = true;
    textureCache.prepare({requestSprite}, requests);
    const TextureResource* resource = textureCache.find(tileset.imageAssetId);
    if (!resource || !resource->loaded) return;

    const int mouseX = GetMouseX();
    const int mouseY = GetMouseY();
    const std::size_t count = std::min(thumbRects.size(), tileset.tiles.size());
    for (std::size_t i = 0; i < count; ++i) {
        const ViewportRect& raw = thumbRects[i];
        if (!raw.valid()) continue;

        // Clip to the Inspector's own visible bounds (clipRect): a thumb
        // scrolled toward the panel's edge is cut off there, never painted
        // over whatever sits below the panel (the Console). Scale/centering
        // below still use the *unclipped* slot so a partially-visible tile
        // looks like a clipped continuation of the full thumbnail, not a
        // squished miniature of the visible sliver.
        const int cx0 = std::max(raw.x, clipRect.x);
        const int cy0 = std::max(raw.y, clipRect.y);
        const int cx1 = std::min(raw.x + raw.width, clipRect.x + clipRect.width);
        const int cy1 = std::min(raw.y + raw.height, clipRect.y + clipRect.height);
        if (cx1 <= cx0 || cy1 <= cy0) continue;   // fully outside the visible panel

        const TileDefinition& tile = tileset.tiles[i];
        if (tile.width <= 0 || tile.height <= 0) continue;

        const Rectangle slotArea{
            static_cast<float>(raw.x), static_cast<float>(raw.y),
            static_cast<float>(raw.width), static_cast<float>(raw.height)};
        BeginScissorMode(cx0, cy0, cx1 - cx0, cy1 - cy0);
        drawTransparencyChecker(slotArea, slotArea);

        // Pixel-friendly: integer upscale when the tile fits its slot, plain
        // fit otherwise (mirrors the animation timeline thumbnails' own rule).
        float scale = std::min(
            slotArea.width  / static_cast<float>(tile.width),
            slotArea.height / static_cast<float>(tile.height));
        if (scale > 1.f) scale = std::floor(scale);
        if (scale > 0.f) {
            const float destW = static_cast<float>(tile.width) * scale;
            const float destH = static_cast<float>(tile.height) * scale;
            const Rectangle dest{
                slotArea.x + (slotArea.width - destW) * 0.5f,
                slotArea.y + (slotArea.height - destH) * 0.5f,
                destW, destH,
            };
            const Rectangle source{
                static_cast<float>(tile.x), static_cast<float>(tile.y),
                static_cast<float>(tile.width), static_cast<float>(tile.height),
            };
            DrawTexturePro(resource->texture, source, dest, Vector2{0.f, 0.f}, 0.f, WHITE);
        }

        const bool selected = selectedTileId && *selectedTileId == tile.id;
        const bool hovered = mouseX >= cx0 && mouseX < cx1 && mouseY >= cy0 && mouseY < cy1;
        if (selected) {
            DrawRectangleRec(slotArea, Color{59, 130, 246, 40});
            DrawRectangleLinesEx(slotArea, 2.f, Color{96, 165, 250, 255});
        } else if (hovered) {
            DrawRectangleLinesEx(slotArea, 1.5f, Color{212, 212, 216, 200});
        } else {
            DrawRectangleLinesEx(slotArea, 1.f, Color{212, 212, 216, 60});
        }
        EndScissorMode();
    }
}

} // namespace ArtCade::EditorNative
