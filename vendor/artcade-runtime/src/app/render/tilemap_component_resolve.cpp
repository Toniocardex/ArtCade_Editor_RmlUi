#include "tilemap_component_resolve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>

namespace ArtCade::AppRender {
namespace {

void addIssue(
    TilemapComponentResolveResult& result,
    TilemapResolveIssueCode code,
    TileId tileId = {},
    int chunkX = 0,
    int chunkY = 0) {
    result.issues.push_back(TilemapResolveIssue{
        code, std::move(tileId), chunkX, chunkY,
    });
}

bool validTileDefinition(const TileDefinition& tile) {
    return tile.x >= 0 && tile.y >= 0 && tile.width > 0 && tile.height > 0;
}

bool checkedCellCoordinate(int chunk, int local, int chunkSize, int& out) {
    const std::int64_t value = static_cast<std::int64_t>(chunk)
        * static_cast<std::int64_t>(chunkSize)
        + static_cast<std::int64_t>(local);
    if (value < static_cast<std::int64_t>(std::numeric_limits<int>::min())
        || value > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
        return false;
    }
    out = static_cast<int>(value);
    return true;
}

} // namespace

TilemapComponentResolveResult resolveTilemapComponent(
    const TilemapComponent& component,
    const TilesetAsset& tileset) {
    TilemapComponentResolveResult result;

    if (component.tilesetAssetId.empty()) {
        addIssue(result, TilemapResolveIssueCode::EmptyTilesetAssetId);
        return result;
    }
    if (tileset.assetId != component.tilesetAssetId) {
        addIssue(result, TilemapResolveIssueCode::TilesetAssetMismatch);
        return result;
    }
    if (tileset.imageAssetId.empty()) {
        addIssue(result, TilemapResolveIssueCode::EmptyImageAssetId);
        return result;
    }
    if (component.chunkSize <= 0) {
        addIssue(result, TilemapResolveIssueCode::InvalidChunkSize);
        return result;
    }
    if (!std::isfinite(component.cellSize.x) || !std::isfinite(component.cellSize.y)
        || component.cellSize.x <= 0.f || component.cellSize.y <= 0.f) {
        addIssue(result, TilemapResolveIssueCode::InvalidCellSize);
        return result;
    }

    const std::size_t chunkSize = static_cast<std::size_t>(component.chunkSize);
    if (chunkSize > std::numeric_limits<std::size_t>::max() / chunkSize) {
        addIssue(result, TilemapResolveIssueCode::ChunkSizeOverflow);
        return result;
    }
    const std::size_t expectedCells = chunkSize * chunkSize;

    std::unordered_map<TileId, const TileDefinition*> tilesById;
    tilesById.reserve(tileset.tiles.size());
    for (const TileDefinition& tile : tileset.tiles) {
        tilesById.emplace(tile.id, &tile);
    }

    ResolvedTilemapDraw draw;
    draw.imageAssetId = tileset.imageAssetId;
    draw.cellSize = component.cellSize;

    for (const TilemapChunk& chunk : component.chunks) {
        const std::size_t count = std::min(chunk.cells.size(), expectedCells);
        for (std::size_t index = 0; index < count; ++index) {
            const TilemapCell& cell = chunk.cells[index];
            if (!cell) continue;
            if (cell->flags != TileTransformFlags::None) {
                addIssue(result, TilemapResolveIssueCode::UnsupportedCellTransform,
                         cell->tileId, chunk.chunkX, chunk.chunkY);
                continue;
            }

            const auto tileIt = tilesById.find(cell->tileId);
            if (tileIt == tilesById.end()) {
                addIssue(result, TilemapResolveIssueCode::MissingTileDefinition,
                         cell->tileId, chunk.chunkX, chunk.chunkY);
                continue;
            }
            const TileDefinition& tile = *tileIt->second;
            if (!validTileDefinition(tile)) {
                addIssue(result, TilemapResolveIssueCode::InvalidTileDefinition,
                         cell->tileId, chunk.chunkX, chunk.chunkY);
                continue;
            }

            const int localX = static_cast<int>(index % chunkSize);
            const int localY = static_cast<int>(index / chunkSize);
            int cellX = 0;
            int cellY = 0;
            if (!checkedCellCoordinate(chunk.chunkX, localX, component.chunkSize, cellX)
                || !checkedCellCoordinate(chunk.chunkY, localY, component.chunkSize, cellY)) {
                addIssue(result, TilemapResolveIssueCode::InvalidChunkCoordinate,
                         cell->tileId, chunk.chunkX, chunk.chunkY);
                continue;
            }

            constexpr float kHalfTexel = 0.5f;
            const bool canInset = tile.width > 1 && tile.height > 1;
            draw.cells.push_back(ResolvedTilemapCell{cellX, cellY});
            draw.regions.push_back(Modules::SpriteRegionDraw{
                static_cast<float>(tile.x) + (canInset ? kHalfTexel : 0.f),
                static_cast<float>(tile.y) + (canInset ? kHalfTexel : 0.f),
                static_cast<float>(tile.width) - (canInset ? 2.f * kHalfTexel : 0.f),
                static_cast<float>(tile.height) - (canInset ? 2.f * kHalfTexel : 0.f),
                static_cast<float>(cellX) * component.cellSize.x,
                static_cast<float>(cellY) * component.cellSize.y,
                component.cellSize.x,
                component.cellSize.y,
            });
        }
    }

    result.draw = std::move(draw);
    return result;
}

} // namespace ArtCade::AppRender
