#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"
#include "editor-native/model/authored_transform.h"

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

// =============================================================================
// Entity authoring commands — operate on a SceneInstanceDef in an explicit scene.
// =============================================================================

/** Place a new instance of an object type in a scene. Invalidates Hierarchy | Inspector | Viewport. */
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
    // Gates the target-layer lock check to the first apply() only - a later
    // redo must reuse this exact command and never be blocked by whatever the
    // layer's lock state happens to be at redo time.
    bool        captured_ = false;
};

// Place the first entity in a project with an empty object-type catalog: it
// creates a real ObjectTypeDef and an instance referencing it, atomically, so
// the new instance immediately resolves to a persisted type (no "Entity"
// sentinel) and object-type-scoped components work at once. One UI gesture, one
// undo entry. The ids are generated once by the caller and kept, so redo
// re-applies the same ids without generating new ones. Invalidates Hierarchy |
// Inspector | Viewport.
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
    bool        captured_ = false;   // see CreateEntityCommand::captured_
};

/** Remove one placed instance. Invalidates Hierarchy | Inspector | Viewport. */
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

/** Place a copy of an existing instance — same object type and per-instance
 *  overrides (sprite, layer, visibility, local variables), fresh id/name/
 *  position. Invalidates Hierarchy | Inspector | Viewport. */
class CloneInstanceCommand final : public EditorCommand {
public:
    CloneInstanceCommand(SceneId sceneId, EntityId sourceId, EntityId newId,
                         std::string newName, Vec2 newPosition);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "CloneInstance"; }

private:
    SceneId     sceneId_;
    EntityId    sourceId_;
    EntityId    newId_;
    std::string newName_;
    Vec2        newPosition_{};
    bool        captured_ = false;   // see CreateEntityCommand::captured_
};

/** Patch authored Transform fields (position / rotation / scale). Never
 *  touches velocity. Invalidates Inspector | Viewport. */
class SetEntityTransformCommand final : public EditorCommand {
public:
    SetEntityTransformCommand(SceneId sceneId, EntityId id, AuthoredTransformPatch patch);
    // Position-only sugar — same command, patch.position only.
    SetEntityTransformCommand(SceneId sceneId, EntityId id, Vec2 position)
        : SetEntityTransformCommand(std::move(sceneId), id, AuthoredTransformPatch{position}) {}

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetEntityTransform"; }

private:
    SceneId                sceneId_;
    EntityId               id_;
    AuthoredTransformPatch patch_;
    AuthoredTransformPatch undoPatch_{};
    bool                   captured_ = false;
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

/** Set the root visibility of one scene instance. This is distinct from the
 *  SpriteRenderer's visibility: false gates the entire instance in Edit and
 *  Play rendering, including tilemaps and editor placeholders. */
class SetInstanceVisibleCommand final : public EditorCommand {
public:
    SetInstanceVisibleCommand(SceneId sceneId, EntityId id, bool visible);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetInstanceVisible"; }

private:
    SceneId sceneId_;
    EntityId id_;
    bool next_ = true;
    bool previous_ = true;
    bool captured_ = false;
};

/** Rename an object type's display name (shared by every instance of that
    type - not the same as RenameEntityCommand, which renames one instance). */
class RenameObjectTypeCommand final : public EditorCommand {
public:
    RenameObjectTypeCommand(std::string objectTypeId, std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameObjectType"; }

private:
    std::string objectTypeId_;
    std::string newName_;
    std::string oldName_;
    bool        captured_ = false;
};

} // namespace ArtCade::EditorNative
