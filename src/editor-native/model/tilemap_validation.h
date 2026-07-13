#pragma once

#include "core/types.h"

#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;

// Generous bound for a 2D editor's chunk edge length; also keeps
// chunkSize*chunkSize (used to check a chunk's cell count) well clear of
// int overflow once chunkSize itself has been validated against this bound.
constexpr int kMaxTilemapChunkSize = 256;

// Validates a TilemapComponent's own fields plus every cross-reference into
// the project (tileset existence, per-cell tile id existence). Returns the
// failure reason, or nullopt if valid. This is the single rule set shared by
// AddTilemapComponentCommand, SetTilemapTilesetCommand (validating a
// hypothetical post-change copy) and ProjectValidator::validate - it must
// not be duplicated at any of those call sites.
std::optional<std::string> validateTilemapComponent(
    const ProjectDocument& document, const TilemapComponent& component);

// What a re-slice of `tilesetAssetId` to `newTiles` would do to the painted
// tilemaps that reference it: which currently-referenced tile ids disappear,
// and how many painted cells / tilemap components that orphans. Read-only
// projection - drives the confirm dialog shown before Apply and the
// post-apply log line; ChangeTilesetSlicingCommand independently re-derives
// the orphaned cells it clears (the UI's counts are never the authority).
struct TilesetResliceImpact {
    int removedReferencedTiles = 0;   // distinct removed ids painted somewhere
    int orphanedCells = 0;            // painted cells whose tile id disappears
    int affectedTilemaps = 0;         // tilemap components with >= 1 such cell
};
TilesetResliceImpact computeTilesetResliceImpact(
    const ProjectDocument& document, const AssetId& tilesetAssetId,
    const std::vector<TileDefinition>& newTiles);

} // namespace ArtCade::EditorNative
