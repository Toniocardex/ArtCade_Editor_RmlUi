#include "editor-native/commands/linear_mover_commands.h"

#include "editor-native/model/project_document.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
// Edit-mode has no viewport visual for motion, so changes invalidate only the
// Inspector (motion is observed in Play, which renders every frame).
constexpr EditorInvalidation kMoverInvalidation = EditorInvalidation::Inspector;

const EntityDef* objectTypeOf(const ProjectDocument& document, const std::string& id) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(id);
    return it == types.end() ? nullptr : &it->second;
}

const LinearMoverComponent* moverOf(const ProjectDocument& document, const std::string& id) {
    const EntityDef* type = objectTypeOf(document, id);
    return (type && type->linearMover) ? &*type->linearMover : nullptr;
}

bool hasOneWayCollider(const EntityDef& type) {
    return type.boxCollider2D
        && type.boxCollider2D->mode == BoxColliderMode::OneWayPlatform;
}

DomainChange added(const std::string& id) {
    return DomainChange::objectTypeComponentAdded(id, ComponentKind::LinearMover);
}
DomainChange removed(const std::string& id) {
    return DomainChange::objectTypeComponentRemoved(id, ComponentKind::LinearMover);
}
DomainChange changed(const std::string& id) {
    return DomainChange::objectTypeComponentChanged(id, ComponentKind::LinearMover);
}
} // namespace

AddLinearMoverCommand::AddLinearMoverCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult AddLinearMoverCommand::apply(ProjectDocument& document) {
    const EntityDef* type = objectTypeOf(document, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);
    // One movement writer per object type.
    if (type->topDownController || type->platformerController) {
        return EditorOperationResult::failure(
            "Object type already has a movement driver (remove it first)");
    }
    if (hasOneWayCollider(*type)) {
        return EditorOperationResult::failure(
            "OneWayPlatform does not support movement drivers");
    }
    if (type->linearMover.has_value()) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!document.addLinearMover(objectTypeId_, LinearMoverComponent{})) {
        return EditorOperationResult::failure("Failed to add LinearMover");
    }
    return EditorOperationResult::success(kMoverInvalidation, added(objectTypeId_));
}

EditorOperationResult AddLinearMoverCommand::undo(ProjectDocument& document) {
    if (!document.removeLinearMover(objectTypeId_)) {
        return EditorOperationResult::failure("Cannot undo LinearMover add");
    }
    return EditorOperationResult::success(kMoverInvalidation, removed(objectTypeId_));
}

RemoveLinearMoverCommand::RemoveLinearMoverCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult RemoveLinearMoverCommand::apply(ProjectDocument& document) {
    const LinearMoverComponent* current = moverOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no LinearMover");
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
    }
    if (!document.removeLinearMover(objectTypeId_)) {
        return EditorOperationResult::failure("Failed to remove LinearMover");
    }
    return EditorOperationResult::success(kMoverInvalidation, removed(objectTypeId_));
}

EditorOperationResult RemoveLinearMoverCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addLinearMover(objectTypeId_, removed_)) {
        return EditorOperationResult::failure("Cannot undo LinearMover removal");
    }
    return EditorOperationResult::success(kMoverInvalidation, added(objectTypeId_));
}

SetLinearMoverDirectionCommand::SetLinearMoverDirectionCommand(std::string objectTypeId,
                                                               Vec2 direction)
    : objectTypeId_(std::move(objectTypeId)), next_(direction) {}

EditorOperationResult SetLinearMoverDirectionCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(next_.x) || !std::isfinite(next_.y)) {
        return EditorOperationResult::failure("LinearMover direction is invalid");
    }
    const LinearMoverComponent* current = moverOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no LinearMover");
    if (current->directionX == next_.x && current->directionY == next_.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = Vec2{current->directionX, current->directionY};
        captured_ = true;
    }
    if (!document.setLinearMoverDirection(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set LinearMover direction");
    }
    return EditorOperationResult::success(kMoverInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetLinearMoverDirectionCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setLinearMoverDirection(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo LinearMover direction change");
    }
    return EditorOperationResult::success(kMoverInvalidation, changed(objectTypeId_));
}

SetLinearMoverSpeedCommand::SetLinearMoverSpeedCommand(std::string objectTypeId, float speed)
    : objectTypeId_(std::move(objectTypeId)), next_(speed) {}

EditorOperationResult SetLinearMoverSpeedCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(next_) || next_ < 0.f) {
        return EditorOperationResult::failure("LinearMover speed must be >= 0");
    }
    const LinearMoverComponent* current = moverOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no LinearMover");
    if (current->speed == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = current->speed;
        captured_ = true;
    }
    if (!document.setLinearMoverSpeed(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set LinearMover speed");
    }
    return EditorOperationResult::success(kMoverInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetLinearMoverSpeedCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setLinearMoverSpeed(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo LinearMover speed change");
    }
    return EditorOperationResult::success(kMoverInvalidation, changed(objectTypeId_));
}

} // namespace ArtCade::EditorNative
