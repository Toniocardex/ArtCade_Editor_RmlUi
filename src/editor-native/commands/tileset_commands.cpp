#include "editor-native/commands/tileset_commands.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/tilemap_cell_access.h"
#include "editor-native/model/tileset_slicing.h"

#include <string>
#include <unordered_set>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kAddInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Inspector;
constexpr EditorInvalidation kRemoveInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Inspector | EditorInvalidation::Viewport;
constexpr EditorInvalidation kRenameInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Inspector;

bool validSlicing(const TilesetSlicing& slicing) {
    return slicing.tileWidth > 0 && slicing.tileHeight > 0
        && slicing.marginX >= 0 && slicing.marginY >= 0
        && slicing.spacingX >= 0 && slicing.spacingY >= 0;
}
} // namespace

AssetId uniqueTilesetAssetId(const ProjectDocument& document, const AssetId& imageAssetId) {
    std::string base = imageAssetId.empty() ? "tileset" : imageAssetId;
    std::string id = base + ".tileset";
    int n = 2;
    while (document.hasTilesetAsset(id)) {
        id = base + "-" + std::to_string(n++) + ".tileset";
    }
    return id;
}

AddTilesetAssetCommand::AddTilesetAssetCommand(AssetId assetId, std::string name,
                                               AssetId imageAssetId, TilesetSlicing slicing,
                                               std::vector<TileDefinition> tiles)
    : assetId_(std::move(assetId)), name_(std::move(name)),
      imageAssetId_(std::move(imageAssetId)), slicing_(slicing), tiles_(std::move(tiles)) {}

EditorOperationResult AddTilesetAssetCommand::apply(ProjectDocument& document) {
    if (assetId_.empty() || imageAssetId_.empty()) {
        return EditorOperationResult::failure("Tileset asset needs an id and a source image");
    }
    if (document.hasTilesetAsset(assetId_)) {
        return EditorOperationResult::failure("Tileset asset already exists: " + assetId_);
    }
    if (!document.hasImageAsset(imageAssetId_)) {
        return EditorOperationResult::failure("Unknown source image asset");
    }
    if (!validSlicing(slicing_)) {
        return EditorOperationResult::failure("Invalid tileset slicing");
    }
    TilesetAsset asset;
    asset.assetId      = assetId_;
    asset.name         = name_;
    asset.imageAssetId = imageAssetId_;
    asset.slicing      = slicing_;
    asset.tiles        = tiles_;
    if (!document.addTilesetAsset(asset)) {
        return EditorOperationResult::failure("Failed to add tileset asset");
    }
    return EditorOperationResult::success(kAddInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult AddTilesetAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeTilesetAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo tileset asset add");
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

RemoveTilesetAssetCommand::RemoveTilesetAssetCommand(AssetId assetId)
    : assetId_(std::move(assetId)) {}

EditorOperationResult RemoveTilesetAssetCommand::apply(ProjectDocument& document) {
    const TilesetAsset* current = document.findTilesetAsset(assetId_);
    if (!current) return EditorOperationResult::failure("Unknown tileset asset: " + assetId_);
    // A TilemapComponent can hold an entire hand-painted level; unlike
    // SpriteAnimator's own reference cleanup (which loses at most a few clip
    // fields), silently deleting one on tileset removal is too easy to
    // trigger by accident from an asset-panel click. Reject instead - a
    // separate, explicitly-named destructive command can add a cascade
    // later if that's ever actually wanted.
    int referencing = 0;
    for (const auto& [sceneId, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (instance.tilemap && instance.tilemap->tilesetAssetId == assetId_) ++referencing;
        }
    }
    if (referencing > 0) {
        return EditorOperationResult::failure(
            "Tileset asset is used by " + std::to_string(referencing)
            + " tilemap component(s) - remove or reassign them first");
    }
    if (!captured_) {
        removed_  = *current;
        captured_ = true;
    }
    if (!document.removeTilesetAsset(assetId_)) {
        return EditorOperationResult::failure("Failed to remove tileset asset");
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveTilesetAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addTilesetAsset(removed_)) {
        return EditorOperationResult::failure("Cannot undo tileset asset removal");
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

RenameTilesetCommand::RenameTilesetCommand(AssetId assetId, std::string name)
    : assetId_(std::move(assetId)), newName_(std::move(name)) {}

EditorOperationResult RenameTilesetCommand::apply(ProjectDocument& document) {
    const TilesetAsset* asset = document.findTilesetAsset(assetId_);
    if (!asset) {
        return EditorOperationResult::failure("No tileset asset with that id");
    }
    if (newName_.empty()) {
        return EditorOperationResult::failure("Name cannot be empty");
    }
    if (asset->name == newName_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        oldName_  = asset->name;
        captured_ = true;
    }
    if (!document.setTilesetName(assetId_, newName_)) {
        return EditorOperationResult::failure("Failed to rename tileset asset");
    }
    return EditorOperationResult::success(kRenameInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RenameTilesetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setTilesetName(assetId_, oldName_)) {
        return EditorOperationResult::failure("Cannot undo tileset asset rename");
    }
    return EditorOperationResult::success(kRenameInvalidation, DomainChange::assetChanged(assetId_));
}

ChangeTilesetSlicingCommand::ChangeTilesetSlicingCommand(AssetId assetId, TilesetSlicing slicing,
                                                         std::vector<TileDefinition> tiles)
    : assetId_(std::move(assetId)), newSlicing_(slicing), newTiles_(std::move(tiles)) {}

EditorOperationResult ChangeTilesetSlicingCommand::apply(ProjectDocument& document) {
    const TilesetAsset* asset = document.findTilesetAsset(assetId_);
    if (!asset) {
        return EditorOperationResult::failure("No tileset asset with that id");
    }
    if (!validSlicing(newSlicing_)) {
        return EditorOperationResult::failure("Invalid tileset slicing");
    }
    if (sameTilesetSlicing(asset->slicing, newSlicing_)
        && sameTileDefinitions(asset->tiles, newTiles_)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }

    // Cascade: build the cleared copy of every painted tilemap that would be
    // orphaned, entirely before the first document mutation - each copy only
    // references surviving ids by construction, so the apply below cannot
    // leave a cell pointing at a missing tile id. Recomputed on every apply
    // (redo runs against the same pre-change state undo restored); only the
    // pre-clear captures are guarded by captured_.
    std::unordered_set<std::string> newIds;
    for (const TileDefinition& tile : newTiles_) newIds.insert(tile.id);
    struct PendingClear {
        SceneId          sceneId;
        EntityId         entityId = 0;
        TilemapComponent after;
    };
    std::vector<PendingClear> pendingClears;
    std::vector<ClearedTilemap> captures;
    for (const auto& [sceneId, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (!instance.tilemap || instance.tilemap->tilesetAssetId != assetId_) {
                continue;
            }
            TilemapComponent cleared = *instance.tilemap;
            bool changed = false;
            for (TilemapChunk& chunk : cleared.chunks) {
                for (TilemapCell& cell : chunk.cells) {
                    if (cell.has_value() && newIds.count(cell->tileId) == 0) {
                        cell.reset();
                        changed = true;
                    }
                }
            }
            if (!changed) continue;
            pruneEmptyChunks(cleared);
            captures.push_back(ClearedTilemap{sceneId, instance.id, *instance.tilemap});
            pendingClears.push_back(PendingClear{sceneId, instance.id, std::move(cleared)});
        }
    }

    if (!captured_) {
        oldSlicing_      = asset->slicing;
        oldTiles_        = asset->tiles;
        clearedTilemaps_ = std::move(captures);
        captured_        = true;
    }
    if (!document.setTilesetSlicing(assetId_, newSlicing_, newTiles_)) {
        return EditorOperationResult::failure("Failed to change tileset slicing");
    }
    for (const PendingClear& clear : pendingClears) {
        if (!document.setTilemapComponent(clear.sceneId, clear.entityId, clear.after)) {
            // Unreachable single-threaded: the instance was found above in
            // this same apply. Surface it loudly rather than continue with a
            // half-applied cascade.
            return EditorOperationResult::failure(
                "Tileset slicing cascade lost a tilemap instance mid-apply");
        }
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult ChangeTilesetSlicingCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setTilesetSlicing(assetId_, oldSlicing_, oldTiles_)) {
        return EditorOperationResult::failure("Cannot undo tileset slicing change");
    }
    for (const ClearedTilemap& cleared : clearedTilemaps_) {
        if (!document.setTilemapComponent(cleared.sceneId, cleared.entityId, cleared.before)) {
            return EditorOperationResult::failure("Cannot undo tileset slicing cascade");
        }
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
