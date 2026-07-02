#include "editor-native/commands/scene_commands.h"

#include "editor-native/model/project_document.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

// ----------------------------------------------------------------------------
// CreateSceneCommand
// ----------------------------------------------------------------------------
CreateSceneCommand::CreateSceneCommand(SceneId id, std::string name)
    : id_(std::move(id)), name_(std::move(name)) {}

namespace {
// Adding or removing a scene changes the scene list (Hierarchy), the project
// (Project), what the viewport may show (Viewport) and — because it can change
// the start scene or the set of valid Play targets — the toolbar (Toolbar).
constexpr EditorInvalidation kSceneStructureInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Viewport
    | EditorInvalidation::Project | EditorInvalidation::Toolbar;
} // namespace

EditorOperationResult CreateSceneCommand::apply(ProjectDocument& document) {
    if (id_.empty()) {
        return EditorOperationResult::failure("Scene id cannot be empty");
    }
    if (document.hasScene(id_)) {
        return EditorOperationResult::failure("A scene with that id already exists");
    }
    const bool wasFirstScene = document.data().scenes.empty();
    previousStart_ = document.startSceneId();
    if (!document.createScene(id_, name_)) {
        return EditorOperationResult::failure("Failed to create scene");
    }
    // Keep the persisted invariant: a project that has scenes has a valid start
    // scene. The very first scene becomes it (the workspace is left untouched).
    if (wasFirstScene) {
        document.setStartSceneId(id_);
        assignedStart_ = true;
    }
    return EditorOperationResult::success(kSceneStructureInvalidation,
                                          DomainChange::sceneAdded(id_));
}

EditorOperationResult CreateSceneCommand::undo(ProjectDocument& document) {
    if (!document.deleteScene(id_)) {
        return EditorOperationResult::failure("Cannot undo scene creation");
    }
    if (assignedStart_) {
        document.setStartSceneId(previousStart_);   // empty again for the first scene
    }
    return EditorOperationResult::success(kSceneStructureInvalidation,
                                          DomainChange::sceneRemoved(id_));
}

// ----------------------------------------------------------------------------
// SetStartSceneCommand
// ----------------------------------------------------------------------------
SetStartSceneCommand::SetStartSceneCommand(SceneId nextSceneId)
    : next_(std::move(nextSceneId)) {}

EditorOperationResult SetStartSceneCommand::apply(ProjectDocument& document) {
    if (!document.hasScene(next_)) {
        return EditorOperationResult::failure("No such scene to set as start");
    }
    if (document.startSceneId() == next_) {
        // Already the start scene: succeed but change nothing, so the coordinator
        // records no undo entry and bumps no revision.
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = document.startSceneId();
        captured_ = true;
    }
    if (!document.setStartSceneId(next_)) {
        return EditorOperationResult::failure("Failed to set start scene");
    }
    return EditorOperationResult::success(EditorInvalidation::Hierarchy
                                          | EditorInvalidation::Toolbar
                                          | EditorInvalidation::Project,
                                          DomainChange::startSceneChanged(next_));
}

EditorOperationResult SetStartSceneCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setStartSceneId(previous_)) {
        return EditorOperationResult::failure("Cannot undo start scene change");
    }
    return EditorOperationResult::success(EditorInvalidation::Hierarchy
                                          | EditorInvalidation::Toolbar
                                          | EditorInvalidation::Project,
                                          DomainChange::startSceneChanged(previous_));
}

// ----------------------------------------------------------------------------
// DeleteSceneCommand
// ----------------------------------------------------------------------------
DeleteSceneCommand::DeleteSceneCommand(SceneId id)
    : id_(std::move(id)) {}

EditorOperationResult DeleteSceneCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(id_);
    if (!scene) {
        return EditorOperationResult::failure("No scene to delete");
    }
    if (!captured_) {
        removed_       = *scene;                 // snapshot instances + settings
        previousStart_ = document.startSceneId();
        captured_      = true;
    }
    if (!document.deleteScene(id_)) {
        return EditorOperationResult::failure("Failed to delete scene");
    }
    return EditorOperationResult::success(kSceneStructureInvalidation,
                                          DomainChange::sceneRemoved(id_));
}

EditorOperationResult DeleteSceneCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.restoreScene(removed_, previousStart_)) {
        return EditorOperationResult::failure("Cannot undo scene deletion");
    }
    return EditorOperationResult::success(kSceneStructureInvalidation,
                                          DomainChange::sceneAdded(id_));
}

// ----------------------------------------------------------------------------
// SetSceneBackgroundCommand
// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// RenameSceneCommand
// ----------------------------------------------------------------------------
RenameSceneCommand::RenameSceneCommand(SceneId sceneId, std::string name)
    : sceneId_(std::move(sceneId)), newName_(std::move(name)) {}

EditorOperationResult RenameSceneCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) {
        return EditorOperationResult::failure("No target scene");
    }
    if (newName_.empty()) {
        return EditorOperationResult::failure("Scene name cannot be empty");
    }
    if (scene->name == newName_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        oldName_ = scene->name;
        captured_ = true;
    }
    if (!document.setSceneName(sceneId_, newName_)) {
        return EditorOperationResult::failure("Failed to rename scene");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
        | EditorInvalidation::Viewport | EditorInvalidation::Toolbar,
        DomainChange::sceneChanged(sceneId_));
}

EditorOperationResult RenameSceneCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setSceneName(sceneId_, oldName_)) {
        return EditorOperationResult::failure("Cannot undo scene rename");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
        | EditorInvalidation::Viewport | EditorInvalidation::Toolbar,
        DomainChange::sceneChanged(sceneId_));
}

// ----------------------------------------------------------------------------
// SetSceneSizeCommand
// ----------------------------------------------------------------------------
SetSceneSizeCommand::SetSceneSizeCommand(SceneId sceneId, Vec2 size)
    : sceneId_(std::move(sceneId)), newSize_(size) {}

EditorOperationResult SetSceneSizeCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(newSize_.x) || !std::isfinite(newSize_.y)
        || newSize_.x <= 0.f || newSize_.y <= 0.f) {
        return EditorOperationResult::failure("Scene size must be positive");
    }
    const Vec2 size{std::round(newSize_.x), std::round(newSize_.y)};  // whole pixels
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) {
        return EditorOperationResult::failure("No target scene");
    }
    if (scene->worldSize.x == size.x && scene->worldSize.y == size.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        oldSize_ = scene->worldSize;
        captured_ = true;
    }
    if (!document.setSceneSize(sceneId_, size)) {
        return EditorOperationResult::failure("Failed to set scene size");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Inspector | EditorInvalidation::Viewport,
        DomainChange::sceneChanged(sceneId_));
}

EditorOperationResult SetSceneSizeCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setSceneSize(sceneId_, oldSize_)) {
        return EditorOperationResult::failure("Cannot undo scene size change");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Inspector | EditorInvalidation::Viewport,
        DomainChange::sceneChanged(sceneId_));
}

SetSceneBackgroundCommand::SetSceneBackgroundCommand(SceneId sceneId, Vec4 color)
    : sceneId_(std::move(sceneId)), newColor_(color) {}

EditorOperationResult SetSceneBackgroundCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) {
        return EditorOperationResult::failure("No target scene");
    }
    if (!captured_) {
        oldColor_ = scene->backgroundColor;
        captured_ = true;
    }
    if (!document.setSceneBackground(sceneId_, newColor_)) {
        return EditorOperationResult::failure("Failed to set background");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Viewport, DomainChange::sceneChanged(sceneId_));
}

EditorOperationResult SetSceneBackgroundCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setSceneBackground(sceneId_, oldColor_)) {
        return EditorOperationResult::failure("Cannot undo background change");
    }
    return EditorOperationResult::success(
        EditorInvalidation::Viewport, DomainChange::sceneChanged(sceneId_));
}

} // namespace ArtCade::EditorNative
