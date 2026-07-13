#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;

// The one id-allocation rule for a tileset created from an image, shared by
// the composition-root create handler and EditorUi's handler-less fallback -
// "<imageId>.tileset", then "<imageId>-2.tileset" etc. on collision.
AssetId uniqueTilesetAssetId(const ProjectDocument& document, const AssetId& imageAssetId);

// Tileset asset catalog commands (Slice 1: data model only — nothing yet
// references a TilesetAsset, so unlike image assets there is no dependency
// to block or clear on removal).
class AddTilesetAssetCommand final : public EditorCommand {
public:
    // `tiles` may be pre-sliced at creation (the caller has the image's pixel
    // dimensions; this layer has neither) so a fresh tileset is usable without
    // a separate first Apply; empty means "not sliced yet".
    AddTilesetAssetCommand(AssetId assetId, std::string name, AssetId imageAssetId,
                           TilesetSlicing slicing, std::vector<TileDefinition> tiles = {});

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddTilesetAsset"; }

private:
    AssetId        assetId_;
    std::string    name_;
    AssetId        imageAssetId_;
    TilesetSlicing slicing_;
    std::vector<TileDefinition> tiles_;
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
//
// Cascade policy (esplicita, AC-DOM-001): painted tilemap cells whose tile id
// disappears from the new tile list are cleared in the same atomic command -
// the document never holds a cell referencing a missing tile id (which
// validateTilemapComponent rejects and Play's strict resolution refuses).
// The command re-derives the orphaned cells itself from the document; it
// never trusts a UI-computed impact. Undo restores the old slicing, the old
// tiles and every cleared component exactly as captured.
class ChangeTilesetSlicingCommand final : public EditorCommand {
public:
    ChangeTilesetSlicingCommand(AssetId assetId, TilesetSlicing slicing,
                                std::vector<TileDefinition> tiles);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "ChangeTilesetSlicing"; }

private:
    struct ClearedTilemap {
        SceneId          sceneId;
        EntityId         entityId = 0;
        TilemapComponent before;   // exact pre-clear component, for undo
    };

    AssetId        assetId_;
    TilesetSlicing newSlicing_;
    std::vector<TileDefinition> newTiles_;
    TilesetSlicing oldSlicing_{};
    std::vector<TileDefinition> oldTiles_;
    std::vector<ClearedTilemap> clearedTilemaps_;
    bool           captured_ = false;
};

} // namespace ArtCade::EditorNative
