#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

// Per-scene render layer commands. `scene.layers` is the single order authority;
// every command targets an explicit SceneId + LayerId. The default layer is
// identified by SceneDef::defaultLayerId and is never removed by these commands.

/** Add a layer at @p index. Invalidates Hierarchy | Inspector | Viewport. */
class AddSceneLayerCommand final : public EditorCommand {
public:
    AddSceneLayerCommand(SceneId sceneId, std::string layerId, std::string name,
                         std::size_t index);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddSceneLayer"; }

private:
    SceneId     sceneId_;
    std::string layerId_;
    std::string name_;
    std::size_t index_;
};

/** Rename a layer. No-op if unchanged. Invalidates Hierarchy | Inspector | Viewport. */
class RenameSceneLayerCommand final : public EditorCommand {
public:
    RenameSceneLayerCommand(SceneId sceneId, std::string layerId, std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameSceneLayer"; }

private:
    SceneId     sceneId_;
    std::string layerId_;
    std::string newName_;
    std::string oldName_;
    bool        captured_ = false;
};

/** Reorder a layer to @p index (render order). Invalidates Inspector | Viewport. */
class MoveSceneLayerCommand final : public EditorCommand {
public:
    MoveSceneLayerCommand(SceneId sceneId, std::string layerId, std::size_t index);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "MoveSceneLayer"; }

private:
    SceneId     sceneId_;
    std::string layerId_;
    std::size_t index_;
    std::size_t oldIndex_ = 0;
    bool        captured_ = false;
};

/**
 * Remove a layer. Policy: the default layer cannot be removed, and a layer that
 * still has instances is rejected (no implicit move). Undo restores it at its
 * original index. Invalidates Hierarchy | Inspector | Viewport.
 */
class RemoveSceneLayerCommand final : public EditorCommand {
public:
    RemoveSceneLayerCommand(SceneId sceneId, std::string layerId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveSceneLayer"; }

private:
    SceneId     sceneId_;
    std::string layerId_;
    std::string removedName_;
    std::size_t index_ = 0;
    bool        captured_ = false;
};

/** Move one instance to another layer of the same scene. Undoable. */
class SetEntityLayerCommand final : public EditorCommand {
public:
    SetEntityLayerCommand(SceneId sceneId, EntityId id, std::string layerId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetEntityLayer"; }

private:
    SceneId     sceneId_;
    EntityId    id_;
    std::string newLayerId_;
    std::string oldLayerId_;
    bool        captured_ = false;
};

} // namespace ArtCade::EditorNative
