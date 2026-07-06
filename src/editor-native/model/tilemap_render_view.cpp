#include "editor-native/model/tilemap_render_view.h"

#include "editor-native/model/tilemap_chunk_math.h"

#include <unordered_map>

namespace ArtCade::EditorNative {

std::vector<SceneFrameTilemapCell> tilemapRenderCells(
    const TilemapComponent& tilemap, const TilesetAsset& tileset, Vec2 originPosition) {
    std::vector<SceneFrameTilemapCell> result;

    std::unordered_map<std::string, const TileDefinition*> tilesById;
    tilesById.reserve(tileset.tiles.size());
    for (const TileDefinition& tile : tileset.tiles) tilesById.emplace(tile.id, &tile);

    for (const TilemapChunk& chunk : tilemap.chunks) {
        for (std::size_t index = 0; index < chunk.cells.size(); ++index) {
            const TilemapCell& cell = chunk.cells[index];
            if (!cell.has_value()) continue;
            const auto tileIt = tilesById.find(cell->tileId);
            if (tileIt == tilesById.end()) continue;
            const TileDefinition& tile = *tileIt->second;

            const int localX = static_cast<int>(index) % tilemap.chunkSize;
            const int localY = static_cast<int>(index) / tilemap.chunkSize;
            const int cellX = chunkAndLocalToCellX(chunk.chunkX, localX, tilemap.chunkSize);
            const int cellY = chunkAndLocalToCellY(chunk.chunkY, localY, tilemap.chunkSize);

            result.push_back(SceneFrameTilemapCell{
                SceneFrameRect{
                    originPosition.x + static_cast<float>(cellX) * tilemap.cellSize.x,
                    originPosition.y + static_cast<float>(cellY) * tilemap.cellSize.y,
                    tilemap.cellSize.x,
                    tilemap.cellSize.y,
                },
                SceneFrameRect{
                    static_cast<float>(tile.x), static_cast<float>(tile.y),
                    static_cast<float>(tile.width), static_cast<float>(tile.height),
                },
            });
        }
    }
    return result;
}

} // namespace ArtCade::EditorNative
