#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"
#include "editor-native/model/tilemap_cell_access.h"

#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Tilemap is instance-owned (one grid per placed entity, ADR-0001), unlike
// BoxCollider2D/LinearMover/etc which are object-type owned - so every
// command here is addressed by (sceneId, id), mirroring the SpriteAnimator
// command family exactly.

// "Create Tilemap Entity": one user gesture, one undo entry. Atomically
// creates a new ObjectTypeDef, its first instance on the given layer, and an
// instance-owned TilemapComponent bound to an existing tileset (cellSize
// derived from the tileset's slicing). Mirrors CreateEntityWithDefaultType-
// Command's staged-commit contract: all validation happens up front, then a
// single commitStagedCommand, so no partial mutation is possible. Undo
// removes instance + object type together (fails if the type gained other
// instances meanwhile, same as CreateEntityWithDefaultTypeCommand).
class CreateTilemapEntityCommand final : public EditorCommand {
public:
    CreateTilemapEntityCommand(SceneId sceneId, EntityId id,
                               std::string objectTypeId, std::string objectTypeName,
                               std::string instanceName, Vec2 position,
                               std::string layerId, AssetId tilesetAssetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "CreateTilemapEntity"; }

private:
    SceneId     sceneId_;
    EntityId    id_ = 0;
    std::string objectTypeId_;
    std::string objectTypeName_;
    std::string instanceName_;
    Vec2        position_{};
    std::string layerId_;   // "" = scene default (the caller passes the active layer)
    AssetId     tilesetAssetId_;
    // Gates the layer-lock check to the first apply() only - a later redo
    // reuses this same command and must not be blocked by the layer's lock
    // state at redo time.
    bool        captured_ = false;
};

class AddTilemapComponentCommand final : public EditorCommand {
public:
    AddTilemapComponentCommand(SceneId sceneId, EntityId id, TilemapComponent component);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddTilemapComponent"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    TilemapComponent component_{};
    // Gates the layer-lock check to the first apply() only - a later redo
    // reuses this same command and must not be blocked by the layer's lock
    // state at redo time.
    bool captured_ = false;
};

class RemoveTilemapComponentCommand final : public EditorCommand {
public:
    RemoveTilemapComponentCommand(SceneId sceneId, EntityId id);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveTilemapComponent"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    TilemapComponent removed_{};
    bool captured_ = false;
};

class SetTilemapTilesetCommand final : public EditorCommand {
public:
    SetTilemapTilesetCommand(SceneId sceneId, EntityId id, AssetId tilesetAssetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetTilemapTileset"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    AssetId next_;
    AssetId previous_;
    bool captured_ = false;
};

class SetTilemapCellSizeCommand final : public EditorCommand {
public:
    SetTilemapCellSizeCommand(SceneId sceneId, EntityId id, Vec2 cellSize);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetTilemapCellSize"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    Vec2 next_{};
    Vec2 previous_{};
    bool captured_ = false;
};

// The ONLY place a stroke ever touches ProjectDocument: commits every net
// cell change from one Brush/Eraser stroke as a single atomic, undoable
// mutation. Brush and Eraser share this one Command - their only difference
// is whether `after` is populated or nullopt. There is no parallel "erase"
// mutation path.
class PaintTilemapCellsCommand final : public EditorCommand {
public:
    PaintTilemapCellsCommand(SceneId sceneId, EntityId id, std::vector<TilemapCellChange> changes);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "PaintTilemapCells"; }

private:
    SceneId sceneId_;
    EntityId id_ = 0;
    std::vector<TilemapCellChange> changes_;
};

} // namespace ArtCade::EditorNative
