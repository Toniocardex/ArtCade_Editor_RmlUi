#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Tileset asset catalog commands (Slice 1: data model only — nothing yet
// references a TilesetAsset, so unlike image assets there is no dependency
// to block or clear on removal).
class AddTilesetAssetCommand final : public EditorCommand {
public:
    AddTilesetAssetCommand(AssetId assetId, std::string name, AssetId imageAssetId,
                           TilesetSlicing slicing);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddTilesetAsset"; }

private:
    AssetId        assetId_;
    std::string    name_;
    AssetId        imageAssetId_;
    TilesetSlicing slicing_;
};

class RemoveTilesetAssetCommand final : public EditorCommand {
public:
    explicit RemoveTilesetAssetCommand(AssetId assetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveTilesetAsset"; }

private:
    AssetId      assetId_;
    TilesetAsset removed_{};   // captured for an exact undo
    bool         captured_ = false;
};

class RenameTilesetCommand final : public EditorCommand {
public:
    RenameTilesetCommand(AssetId assetId, std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameTileset"; }

private:
    AssetId     assetId_;
    std::string newName_;
    std::string oldName_;
    bool        captured_ = false;
};

// Atomically swaps the slicing config and the tile list. The caller computes
// the final tiles (tilesForSlicing + reconcileTiles against the asset's
// current tiles, using the source image's real pixel dimensions - this layer
// has neither) and passes them in; the command itself does no slicing math.
class ChangeTilesetSlicingCommand final : public EditorCommand {
public:
    ChangeTilesetSlicingCommand(AssetId assetId, TilesetSlicing slicing,
                                std::vector<TileDefinition> tiles);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "ChangeTilesetSlicing"; }

private:
    AssetId        assetId_;
    TilesetSlicing newSlicing_;
    std::vector<TileDefinition> newTiles_;
    TilesetSlicing oldSlicing_{};
    std::vector<TileDefinition> oldTiles_;
    bool           captured_ = false;
};

} // namespace ArtCade::EditorNative
