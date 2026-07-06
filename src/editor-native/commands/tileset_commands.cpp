#include "editor-native/commands/tileset_commands.h"

#include "editor-native/model/project_document.h"

#include <string>
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

AddTilesetAssetCommand::AddTilesetAssetCommand(AssetId assetId, std::string name,
                                               AssetId imageAssetId, TilesetSlicing slicing)
    : assetId_(std::move(assetId)), name_(std::move(name)),
      imageAssetId_(std::move(imageAssetId)), slicing_(slicing) {}

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
    if (!captured_) {
        oldSlicing_ = asset->slicing;
        oldTiles_   = asset->tiles;
        captured_   = true;
    }
    if (!document.setTilesetSlicing(assetId_, newSlicing_, newTiles_)) {
        return EditorOperationResult::failure("Failed to change tileset slicing");
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult ChangeTilesetSlicingCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setTilesetSlicing(assetId_, oldSlicing_, oldTiles_)) {
        return EditorOperationResult::failure("Cannot undo tileset slicing change");
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
