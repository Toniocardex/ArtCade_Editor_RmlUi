#include "editor-native/model/tilemap_render_view.h"

#include "editor-native/model/tilemap_chunk_math.h"

#include <unordered_map>

namespace ArtCade::EditorNative {

SceneFrameRect tilemapCellDestination(Vec2 origin, Vec2 cellSize, int cellX, int cellY) {
    return SceneFrameRect{
        origin.x + static_cast<float>(cellX) * cellSize.x,
        origin.y + static_cast<float>(cellY) * cellSize.y,
        cellSize.x,
        cellSize.y,
    };
}

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
                tilemapCellDestination(originPosition, tilemap.cellSize, cellX, cellY),
                SceneFrameRect{
                    static_cast<float>(tile.x), static_cast<float>(tile.y),
                    static_cast<float>(tile.width), static_cast<float>(tile.height),
                },
            });
        }
    }
    return result;
}

SceneFrameRect tilemapAtlasSourceRect(const SceneFrameRect& rect) {
    constexpr float kHalfTexel = 0.5f;
    if (rect.width <= 2.0f * kHalfTexel || rect.height <= 2.0f * kHalfTexel) return rect;
    return SceneFrameRect{
        rect.x + kHalfTexel,
        rect.y + kHalfTexel,
        rect.width - 2.0f * kHalfTexel,
        rect.height - 2.0f * kHalfTexel,
    };
}

std::optional<std::vector<TilemapResolvedCell>> resolveTilemapCellsStrict(
    const TilemapComponent& tilemap, const TilesetAsset& tileset) {
    std::vector<TilemapResolvedCell> result;

    std::unordered_map<std::string, const TileDefinition*> tilesById;
    tilesById.reserve(tileset.tiles.size());
    for (const TileDefinition& tile : tileset.tiles) tilesById.emplace(tile.id, &tile);

    for (const TilemapChunk& chunk : tilemap.chunks) {
        for (std::size_t index = 0; index < chunk.cells.size(); ++index) {
            const TilemapCell& cell = chunk.cells[index];
            if (!cell.has_value()) continue;
            const auto tileIt = tilesById.find(cell->tileId);
            if (tileIt == tilesById.end()) return std::nullopt;   // atomic failure
            const TileDefinition& tile = *tileIt->second;

            const int localX = static_cast<int>(index) % tilemap.chunkSize;
            const int localY = static_cast<int>(index) / tilemap.chunkSize;
            result.push_back(TilemapResolvedCell{
                chunkAndLocalToCellX(chunk.chunkX, localX, tilemap.chunkSize),
                chunkAndLocalToCellY(chunk.chunkY, localY, tilemap.chunkSize),
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
