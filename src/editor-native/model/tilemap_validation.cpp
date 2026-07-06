#include "editor-native/model/tilemap_validation.h"

#include "editor-native/model/project_document.h"

#include <cmath>
#include <cstdint>
#include <unordered_set>

namespace ArtCade::EditorNative {

std::optional<std::string> validateTilemapComponent(
    const ProjectDocument& document, const TilemapComponent& component) {
    if (component.tilesetAssetId.empty() || !document.hasTilesetAsset(component.tilesetAssetId)) {
        return "Tilemap component references a missing tileset asset";
    }
    if (!std::isfinite(component.cellSize.x) || !std::isfinite(component.cellSize.y)
        || component.cellSize.x <= 0.f || component.cellSize.y <= 0.f) {
        return "Tilemap cell size must be positive";
    }
    if (component.chunkSize < 1 || component.chunkSize > kMaxTilemapChunkSize) {
        return "Tilemap chunk size must be between 1 and " + std::to_string(kMaxTilemapChunkSize);
    }

    const TilesetAsset* tileset = document.findTilesetAsset(component.tilesetAssetId);
    std::unordered_set<std::string> validTileIds;
    if (tileset) {
        for (const TileDefinition& tile : tileset->tiles) validTileIds.insert(tile.id);
    }

    const std::size_t expectedCells =
        static_cast<std::size_t>(component.chunkSize) * static_cast<std::size_t>(component.chunkSize);
    std::unordered_set<std::int64_t> seenChunkCoords;
    for (const TilemapChunk& chunk : component.chunks) {
        const std::int64_t coordKey = (static_cast<std::int64_t>(chunk.chunkX) << 32)
            ^ static_cast<std::uint32_t>(chunk.chunkY);
        if (!seenChunkCoords.insert(coordKey).second) {
            return "Duplicate tilemap chunk coordinate";
        }
        if (chunk.cells.size() != expectedCells) {
            return "Tilemap chunk cell count must equal chunkSize squared";
        }
        for (const TilemapCell& cell : chunk.cells) {
            if (cell.has_value() && validTileIds.count(cell->tileId) == 0) {
                return "Tilemap cell references a missing tile id";
            }
        }
    }
    return std::nullopt;
}

} // namespace ArtCade::EditorNative
