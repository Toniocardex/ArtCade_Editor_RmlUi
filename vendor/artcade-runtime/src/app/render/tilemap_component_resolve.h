#pragma once

#include "../../core/types.h"
#include "../../modules/renderer/include/sprite-region-draw.h"

#include <optional>
#include <vector>

namespace ArtCade::AppRender {

struct ResolvedTilemapCell {
    int cellX = 0;
    int cellY = 0;
};

struct ResolvedTilemapDraw {
    AssetId imageAssetId;
    Vec2 cellSize{};
    // cells[i] and regions[i] describe the same resolved tile. Both vectors
    // are built at cache invalidation boundaries and remain immutable while
    // frame snapshots observe this draw.
    std::vector<ResolvedTilemapCell> cells;
    std::vector<Modules::SpriteRegionDraw> regions;
};

enum class TilemapResolveIssueCode {
    MissingTileset,
    EmptyTilesetAssetId,
    TilesetAssetMismatch,
    EmptyImageAssetId,
    InvalidChunkSize,
    InvalidCellSize,
    ChunkSizeOverflow,
    MissingTileDefinition,
    InvalidTileDefinition,
    UnsupportedCellTransform,
    InvalidChunkCoordinate,
};

struct TilemapResolveIssue {
    TilemapResolveIssueCode code = TilemapResolveIssueCode::InvalidChunkSize;
    TileId tileId;
    int chunkX = 0;
    int chunkY = 0;
};

struct TilemapComponentResolveResult {
    std::optional<ResolvedTilemapDraw> draw;
    std::vector<TilemapResolveIssue> issues;
};

// Resolves static tilemap presentation into local cell coordinates. It never
// logs, never reads editor-native code, and skips invalid individual cells so
// shipped games can still render valid content.
TilemapComponentResolveResult resolveTilemapComponent(
    const TilemapComponent& component,
    const TilesetAsset& tileset);

} // namespace ArtCade::AppRender
