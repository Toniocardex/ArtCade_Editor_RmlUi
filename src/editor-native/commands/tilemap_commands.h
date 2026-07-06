#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

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

} // namespace ArtCade::EditorNative
