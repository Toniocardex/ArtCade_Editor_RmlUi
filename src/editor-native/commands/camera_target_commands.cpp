#include "editor-native/commands/camera_target_commands.h"

#include "editor-native/model/numeric_validation.h"
#include "editor-native/model/project_document.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kCameraTargetInvalidation = EditorInvalidation::Inspector;
constexpr float kDefaultCameraFollowSpeed = 8.f;

const CameraTargetComponent* cameraTargetOf(const ProjectDocument& document,
                                             const SceneId& sceneId, EntityId id) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId, id);
    return (instance && instance->cameraTarget) ? &*instance->cameraTarget : nullptr;
}

DomainChange added(const SceneId& sceneId, EntityId id) {
    return DomainChange::componentAdded(sceneId, id, ComponentKind::CameraTarget);
}
DomainChange removed(const SceneId& sceneId, EntityId id) {
    return DomainChange::componentRemoved(sceneId, id, ComponentKind::CameraTarget);
}
DomainChange changed(const SceneId& sceneId, EntityId id) {
    return DomainChange::componentChanged(sceneId, id, ComponentKind::CameraTarget);
}

bool isFiniteOffset(Vec2 offset) {
    return std::isfinite(offset.x) && std::isfinite(offset.y);
}
} // namespace

AddCameraTargetCommand::AddCameraTargetCommand(SceneId sceneId, EntityId id)
    : sceneId_(std::move(sceneId)), id_(id) {}

EditorOperationResult AddCameraTargetCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId_, id_);
    if (!instance) return EditorOperationResult::failure("Unknown scene instance");
    if (instance->cameraTarget) return EditorOperationResult::success(EditorInvalidation::None);

    CameraTargetComponent component;
    component.followSpeed = kDefaultCameraFollowSpeed;
    if (!document.addCameraTarget(sceneId_, id_, component)) {
        return EditorOperationResult::failure(
            "Camera Target already exists on another instance in this scene");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, added(sceneId_, id_));
}

EditorOperationResult AddCameraTargetCommand::undo(ProjectDocument& document) {
    if (!document.removeCameraTarget(sceneId_, id_)) {
        return EditorOperationResult::failure("Cannot undo Camera Target add");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, removed(sceneId_, id_));
}

RemoveCameraTargetCommand::RemoveCameraTargetCommand(SceneId sceneId, EntityId id)
    : sceneId_(std::move(sceneId)), id_(id) {}

EditorOperationResult RemoveCameraTargetCommand::apply(ProjectDocument& document) {
    const CameraTargetComponent* current = cameraTargetOf(document, sceneId_, id_);
    if (!current) return EditorOperationResult::failure("Instance has no Camera Target");
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
    }
    if (!document.removeCameraTarget(sceneId_, id_)) {
        return EditorOperationResult::failure("Failed to remove Camera Target");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, removed(sceneId_, id_));
}

EditorOperationResult RemoveCameraTargetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addCameraTarget(sceneId_, id_, removed_)) {
        return EditorOperationResult::failure("Cannot undo Camera Target removal");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, added(sceneId_, id_));
}

SetCameraTargetOffsetCommand::SetCameraTargetOffsetCommand(SceneId sceneId, EntityId id, Vec2 offset)
    : sceneId_(std::move(sceneId)), id_(id), next_(offset) {}

EditorOperationResult SetCameraTargetOffsetCommand::apply(ProjectDocument& document) {
    if (!isFiniteOffset(next_)) {
        return EditorOperationResult::failure("Camera Target offset must be finite");
    }
    const CameraTargetComponent* current = cameraTargetOf(document, sceneId_, id_);
    if (!current) return EditorOperationResult::failure("Instance has no Camera Target");
    if (current->offsetX == next_.x && current->offsetY == next_.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = *current;
        captured_ = true;
    }
    CameraTargetComponent updated = *current;
    updated.offsetX = next_.x;
    updated.offsetY = next_.y;
    if (!document.setCameraTarget(sceneId_, id_, updated)) {
        return EditorOperationResult::failure("Failed to set Camera Target offset");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, changed(sceneId_, id_));
}

EditorOperationResult SetCameraTargetOffsetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setCameraTarget(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo Camera Target offset");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, changed(sceneId_, id_));
}

SetCameraTargetFollowSpeedCommand::SetCameraTargetFollowSpeedCommand(
    SceneId sceneId, EntityId id, float followSpeed)
    : sceneId_(std::move(sceneId)), id_(id), next_(followSpeed) {}

EditorOperationResult SetCameraTargetFollowSpeedCommand::apply(ProjectDocument& document) {
    if (!NumericValidation::isNonNegative(next_)) {
        return EditorOperationResult::failure("Camera Target follow speed must be finite and >= 0");
    }
    const CameraTargetComponent* current = cameraTargetOf(document, sceneId_, id_);
    if (!current) return EditorOperationResult::failure("Instance has no Camera Target");
    if (current->followSpeed == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = *current;
        captured_ = true;
    }
    CameraTargetComponent updated = *current;
    updated.followSpeed = next_;
    if (!document.setCameraTarget(sceneId_, id_, updated)) {
        return EditorOperationResult::failure("Failed to set Camera Target follow speed");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, changed(sceneId_, id_));
}

EditorOperationResult SetCameraTargetFollowSpeedCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setCameraTarget(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo Camera Target follow speed");
    }
    return EditorOperationResult::success(kCameraTargetInvalidation, changed(sceneId_, id_));
}

} // namespace ArtCade::EditorNative
