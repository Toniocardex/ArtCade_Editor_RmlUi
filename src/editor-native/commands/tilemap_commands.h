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
