#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// =============================================================================
// Scene authoring commands.
// =============================================================================

/**
 * Create an empty scene. Invalidates Hierarchy | Viewport | Project | Toolbar.
 * When it is the project's first scene it also becomes the start scene, so the
 * invariant "scenes exist => startSceneId is valid" holds (ProjectValidator).
 * This is not auto-selection: the workspace active scene is left untouched.
 */
class CreateSceneCommand final : public EditorCommand {
public:
    CreateSceneCommand(SceneId id, std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "CreateScene"; }

private:
    SceneId     id_;
    std::string name_;
    SceneId     previousStart_{};       // start scene before this command
    bool        assignedStart_ = false; // did this command set the start scene?
};

/**
 * Choose which existing scene is the gameplay start scene. Persistent domain
 * change (not a workspace selection). Invalidates Hierarchy | Toolbar | Project.
 * Setting the scene that is already the start is a no-op that does not enter undo.
 */
class SetStartSceneCommand final : public EditorCommand {
public:
    explicit SetStartSceneCommand(SceneId nextSceneId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetStartScene"; }

private:
    SceneId next_;
    SceneId previous_;
    bool    captured_ = false;
};

/** Delete a scene with its instances. Invalidates Hierarchy | Viewport | Project. */
class DeleteSceneCommand final : public EditorCommand {
public:
    explicit DeleteSceneCommand(SceneId id);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "DeleteScene"; }

private:
    SceneId  id_;
    SceneDef removed_{};        // full snapshot for an exact undo
    SceneId  previousStart_{};  // start-scene id before deletion
    bool     captured_ = false;
};

/** Rename one scene. Invalidates Hierarchy | Inspector | Viewport | Toolbar. */
class RenameSceneCommand final : public EditorCommand {
public:
    RenameSceneCommand(SceneId sceneId, std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameScene"; }

private:
    SceneId     sceneId_;
    std::string newName_;
    std::string oldName_;
    bool        captured_ = false;
};

/**
 * Set one scene's world size (Dimensions). Width/height must be finite and > 0;
 * the value is normalized to whole pixels at commit. Resizing never moves
 * instances. Invalidates Inspector | Viewport.
 */
class SetSceneSizeCommand final : public EditorCommand {
public:
    SetSceneSizeCommand(SceneId sceneId, Vec2 size);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSceneSize"; }

private:
    SceneId sceneId_;
    Vec2    newSize_;
    Vec2    oldSize_{};
    bool    captured_ = false;
};

/** Set one scene's background colour. Invalidates Viewport. */
class SetSceneBackgroundCommand final : public EditorCommand {
public:
    SetSceneBackgroundCommand(SceneId sceneId, Vec4 color);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "SetSceneBackground"; }

private:
    SceneId sceneId_;
    Vec4 newColor_;
    Vec4 oldColor_{};
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
