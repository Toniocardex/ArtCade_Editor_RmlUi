#include "editor-native/commands/top_down_controller_commands.h"

#include "editor-native/model/project_document.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
// No edit-mode viewport visual for movement; changes invalidate the Inspector
// only (motion is observed in Play, which renders every frame).
constexpr EditorInvalidation kControllerInvalidation = EditorInvalidation::Inspector;

const EntityDef* objectTypeOf(const ProjectDocument& document, const std::string& id) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(id);
    return it == types.end() ? nullptr : &it->second;
}

const TopDownControllerComponent* controllerOf(const ProjectDocument& document,
                                               const std::string& id) {
    const EntityDef* type = objectTypeOf(document, id);
    return (type && type->topDownController) ? &*type->topDownController : nullptr;
}

bool hasOneWayCollider(const EntityDef& type) {
    return type.boxCollider2D
        && type.boxCollider2D->mode == BoxColliderMode::OneWayPlatform;
}

DomainChange added(const std::string& id) {
    return DomainChange::objectTypeComponentAdded(id, ComponentKind::TopDownController);
}
DomainChange removed(const std::string& id) {
    return DomainChange::objectTypeComponentRemoved(id, ComponentKind::TopDownController);
}
DomainChange changed(const std::string& id) {
    return DomainChange::objectTypeComponentChanged(id, ComponentKind::TopDownController);
}
} // namespace

AddTopDownControllerCommand::AddTopDownControllerCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult AddTopDownControllerCommand::apply(ProjectDocument& document) {
    const EntityDef* type = objectTypeOf(document, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);
    // One movement writer per object type.
    if (type->linearMover || type->platformerController) {
        return EditorOperationResult::failure(
            "Object type already has a movement driver (remove it first)");
    }
    if (hasOneWayCollider(*type)) {
        return EditorOperationResult::failure(
            "OneWayPlatform does not support movement drivers");
    }
    if (type->topDownController.has_value()) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!document.addTopDownController(objectTypeId_, TopDownControllerComponent{})) {
        return EditorOperationResult::failure("Failed to add TopDownController");
    }
    return EditorOperationResult::success(kControllerInvalidation, added(objectTypeId_));
}

EditorOperationResult AddTopDownControllerCommand::undo(ProjectDocument& document) {
    if (!document.removeTopDownController(objectTypeId_)) {
        return EditorOperationResult::failure("Cannot undo TopDownController add");
    }
    return EditorOperationResult::success(kControllerInvalidation, removed(objectTypeId_));
}

RemoveTopDownControllerCommand::RemoveTopDownControllerCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult RemoveTopDownControllerCommand::apply(ProjectDocument& document) {
    const TopDownControllerComponent* current = controllerOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no TopDownController");
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
    }
    if (!document.removeTopDownController(objectTypeId_)) {
        return EditorOperationResult::failure("Failed to remove TopDownController");
    }
    return EditorOperationResult::success(kControllerInvalidation, removed(objectTypeId_));
}

EditorOperationResult RemoveTopDownControllerCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addTopDownController(objectTypeId_, removed_)) {
        return EditorOperationResult::failure("Cannot undo TopDownController removal");
    }
    return EditorOperationResult::success(kControllerInvalidation, added(objectTypeId_));
}

SetTopDownControllerSpeedCommand::SetTopDownControllerSpeedCommand(std::string objectTypeId,
                                                                  float speed)
    : objectTypeId_(std::move(objectTypeId)), next_(speed) {}

EditorOperationResult SetTopDownControllerSpeedCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(next_) || next_ < 0.f) {
        return EditorOperationResult::failure("TopDownController speed must be >= 0");
    }
    const TopDownControllerComponent* current = controllerOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no TopDownController");
    if (current->maxSpeed == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = current->maxSpeed;
        captured_ = true;
    }
    if (!document.setTopDownControllerSpeed(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set TopDownController speed");
    }
    return EditorOperationResult::success(kControllerInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetTopDownControllerSpeedCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setTopDownControllerSpeed(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo TopDownController speed change");
    }
    return EditorOperationResult::success(kControllerInvalidation, changed(objectTypeId_));
}

} // namespace ArtCade::EditorNative
