#include "tilemap-renderer.h"

#include "../../modules/renderer/include/renderer.h"

namespace ArtCade::TilemapRenderer {

namespace {

const TilesetAsset* resolveTileset(
    const std::string& assetId,
    const std::vector<TilesetAsset>& liveTilesets,
    const std::unordered_map<std::string, TilesetAsset>& startupCache)
{
    if (assetId.empty()) return nullptr;
    for (const auto& t : liveTilesets) {
        if (t.assetId == assetId) return &t;
    }
    auto it = startupCache.find(assetId);
    return it != startupCache.end() ? &it->second : nullptr;
}

const TilesetAsset* resolveCellTileset(
    const TilemapData& tm,
    int sourceIndex,
    const std::vector<TilesetAsset>& liveTilesets,
    const std::unordered_map<std::string, TilesetAsset>& startupCache)
{
    if (sourceIndex > 0
        && sourceIndex <= static_cast<int>(tm.tilesetSources.size())) {
        return resolveTileset(
            tm.tilesetSources[static_cast<size_t>(sourceIndex - 1)].tilesetAssetId,
            liveTilesets,
            startupCache);
    }
    if (!tm.tilesetAssetId.empty())
        return resolveTileset(tm.tilesetAssetId, liveTilesets, startupCache);
    return nullptr;
}

int cellSourceIndex(const TilemapData& tm, int idx) {
    if (idx >= 0 && idx < static_cast<int>(tm.sourceIndices.size()))
        return tm.sourceIndices[static_cast<size_t>(idx)];
    if (!tm.tilesetAssetId.empty()) return 1;
    return 0;
}

void drawLayer(Modules::Renderer& renderer,
               const TilemapData& tm,
               const std::vector<TilesetAsset>& liveTilesets,
               const std::unordered_map<std::string, TilesetAsset>& startupCache,
               const std::unordered_map<int, Vec4>& palette,
               float opacity)
{
    if (tm.cols <= 0 || tm.rows <= 0) return;

    const int n = static_cast<int>(tm.data.size());

    for (int r = 0; r < tm.rows; ++r) {
        for (int c = 0; c < tm.cols; ++c) {
            const int idx = r * tm.cols + c;
            if (idx >= n) continue;
            const int id = tm.data[idx];
            if (id <= 0) continue;

            const float dx = c * tm.tileSize;
            const float dy = r * tm.tileSize;

            const TilesetAsset* ts = resolveCellTileset(
                tm, cellSourceIndex(tm, idx), liveTilesets, startupCache);

            bool drawn = false;
            if (ts && !ts->imageAssetId.empty()) {
                const TileDefinition* def = nullptr;
                if (id > 0 && id <= static_cast<int>(ts->tiles.size()))
                    def = &ts->tiles[static_cast<size_t>(id - 1)];
                if (def && def->width > 0 && def->height > 0) {
                    drawn = renderer.drawSpriteRegion(
                        ts->imageAssetId,
                        static_cast<float>(def->x),
                        static_cast<float>(def->y),
                        static_cast<float>(def->width),
                        static_cast<float>(def->height),
                        dx, dy, tm.tileSize, tm.tileSize, opacity);
                } else if (ts->slicing.tileWidth > 0) {
                    const float tileW =
                        static_cast<float>(ts->slicing.tileWidth);
                    const float tileH =
                        static_cast<float>(ts->slicing.tileHeight);
                    const float stepX = tileW
                        + static_cast<float>(ts->slicing.spacingX);
                    const int sCol = id - 1;
                    drawn = renderer.drawSpriteRegion(
                        ts->imageAssetId,
                        static_cast<float>(ts->slicing.marginX) + sCol * stepX,
                        static_cast<float>(ts->slicing.marginY),
                        tileW, tileH,
                        dx, dy, tm.tileSize, tm.tileSize, opacity);
                }
            }
            if (!drawn) {
                auto it = palette.find(id);
                const Vec4 col = (it != palette.end())
                    ? it->second : Vec4{0.5f, 0.5f, 0.5f, 1.f};
                renderer.drawRect(dx, dy, tm.tileSize, tm.tileSize,
                                  { col.r, col.g, col.b, col.a * opacity });
            }
        }
    }
}

} // namespace

void draw(Modules::Renderer& renderer,
          const TilemapData* mergedTilemap,
          const std::unordered_map<std::string, TilemapData>* tilemapLayers,
          const std::unordered_map<std::string, SceneLayerSettings>& layerSettings,
          const std::vector<SceneLayerDef>& layerStack,
          const std::vector<TilesetAsset>& liveTilesets,
          const std::unordered_map<std::string, TilesetAsset>& startupCache,
          const std::unordered_map<int, Vec4>& palette)
{
    static const TilemapData kEmptyTilemap{};
    static const std::unordered_map<std::string, TilemapData> kEmptyLayers{};
    const TilemapData& merged = mergedTilemap ? *mergedTilemap : kEmptyTilemap;
    const auto& layers = tilemapLayers ? *tilemapLayers : kEmptyLayers;

    if (!layers.empty() && !layerStack.empty()) {
        // SceneDef.layers: index 0 = background → draw first; last = foreground.
        for (const auto& layer : layerStack) {
            SceneLayerSettings settings;
            const auto sit = layerSettings.find(layer.id);
            if (sit != layerSettings.end()) settings = sit->second;
            if (!settings.visible || settings.opacity <= 0.f) continue;
            const auto it = layers.find(layer.id);
            if (it != layers.end())
                drawLayer(renderer, it->second, liveTilesets, startupCache, palette,
                          settings.opacity);
        }
        return;
    }

    drawLayer(renderer, merged, liveTilesets, startupCache, palette, 1.f);
}

} // namespace ArtCade::TilemapRenderer
