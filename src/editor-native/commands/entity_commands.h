#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

// =============================================================================
// Entity authoring commands — operate on a SceneInstanceDef in an explicit scene.
// =============================================================================

/** Place a new instance of an object type in a scene. Invalidates Hierarchy | Viewport. */
class CreateEntityCommand final : public EditorCommand {
public:
    CreateEntityCommand(SceneId sceneId, EntityId id, std::string objectTypeId,
                        std::string instanceName, Vec2 position = {}, std::string layerId = {});

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "CreateEntity"; }

private:
    SceneId     sceneId_;
    EntityId    id_;
    std::string objectTypeId_;
    std::string instanceName_;
    Vec2        position_{};
    std::string layerId_;   // "" = scene default (the caller passes the active layer)
};

// Place the first entity in a project with an empty object-type catalog: it
// creates a real ObjectTypeDef and an instance referencing it, atomically, so
// the new instance immediately resolves to a persisted type (no "Entity"
// sentinel) and object-type-scoped components work at once. One UI gesture, one
// undo entry. The ids are generated once by the caller and kept, so redo
// re-applies the same ids without generating new ones. Invalidates Hierarchy |
// Viewport.
class CreateEntityWithDefaultTypeCommand final : public EditorCommand {
public:
    CreateEntityWithDefaultTypeCommand(SceneId sceneId, EntityId id,
                                       std::string objectTypeId, std::string objectTypeName,
                                       std::string instanceName, Vec2 position = {},
                                       std::string layerId = {});

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "CreateEntityWithDefaultType"; }

private:
    SceneId     sceneId_;
    EntityId    id_;
    std::string objectTypeId_;
    std::string objectTypeName_;
    std::string instanceName_;
    Vec2        position_{};
    std::string layerId_;   // "" = scene default (the caller passes the active layer)
};

/** Remove one placed instance. Invalidates Hierarchy | Viewport. */
class DeleteEntityCommand final : public EditorCommand {
public:
    DeleteEntityCommand(SceneId sceneId, EntityId id);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "DeleteEntity"; }

private:
    SceneId          sceneId_;
    EntityId         id_;
    SceneInstanceDef removed_{};   // snapshot for an exact undo
    std::size_t      index_ = 0;   // original position within the scene's instance list
    bool             captured_ = false;
};

/** Move one instance. Invalidates Inspector | Viewport (prompt §24.4). */
class SetEntityPositionCommand final : public EditorCommand {
public:
    SetEntityPositionCommand(SceneId sceneId, EntityId id, Vec2 position);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetEntityPosition"; }

private:
    SceneId  sceneId_;
    EntityId id_;
    Vec2     newPosition_;
    Vec2     oldPosition_{};
    bool     captured_ = false;
};

/** Rename one instance. Invalidates Hierarchy | Inspector. */
class RenameEntityCommand final : public EditorCommand {
public:
    RenameEntityCommand(SceneId sceneId, EntityId id, std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameEntity"; }

private:
    SceneId     sceneId_;
    EntityId    id_;
    std::string newName_;
    std::string oldName_;
    bool        captured_ = false;
};

} // namespace ArtCade::EditorNative
