#include "editor-native/commands/platformer_controller_commands.h"

#include "editor-native/model/project_document.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
// No edit-mode viewport visual for movement; changes invalidate the Inspector
// only (motion is observed in Play, which renders every frame).
constexpr EditorInvalidation kPlatformerInvalidation = EditorInvalidation::Inspector;

const EntityDef* objectTypeOf(const ProjectDocument& document, const std::string& id) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(id);
    return it == types.end() ? nullptr : &it->second;
}

const PlatformerControllerComponent* platformerOf(const ProjectDocument& document,
                                                  const std::string& id) {
    const EntityDef* type = objectTypeOf(document, id);
    return (type && type->platformerController) ? &*type->platformerController : nullptr;
}

bool hasOneWayCollider(const EntityDef& type) {
    return type.boxCollider2D
        && type.boxCollider2D->mode == BoxColliderMode::OneWayPlatform;
}

// The canonical field each editable value maps onto.
float readField(const PlatformerControllerComponent& c, PlatformerField field) {
    switch (field) {
        case PlatformerField::MoveSpeed: return c.maxSpeed;
        case PlatformerField::JumpSpeed: return c.jumpForce;
        case PlatformerField::Gravity:   return c.customGravity;
    }
    return 0.f;
}

DomainChange added(const std::string& id) {
    return DomainChange::objectTypeComponentAdded(id, ComponentKind::PlatformerController);
}
DomainChange removed(const std::string& id) {
    return DomainChange::objectTypeComponentRemoved(id, ComponentKind::PlatformerController);
}
DomainChange changed(const std::string& id) {
    return DomainChange::objectTypeComponentChanged(id, ComponentKind::PlatformerController);
}
} // namespace

// ----------------------------------------------------------------------------
AddPlatformerControllerCommand::AddPlatformerControllerCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult AddPlatformerControllerCommand::apply(ProjectDocument& document) {
    const EntityDef* type = objectTypeOf(document, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);
    // One movement writer per object type.
    if (type->topDownController || type->linearMover) {
        return EditorOperationResult::failure(
            "Object type already has a movement driver (remove it first)");
    }
    if (hasOneWayCollider(*type)) {
        return EditorOperationResult::failure(
            "OneWayPlatform does not support movement drivers");
    }
    if (type->platformerController.has_value()) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    // The editor's recommended starting values (the other canonical fields keep
    // their defaults).
    PlatformerControllerComponent component;
    component.maxSpeed      = 180.f;
    component.jumpForce     = 420.f;
    component.customGravity = 1200.f;
    if (!document.addPlatformerController(objectTypeId_, component)) {
        return EditorOperationResult::failure("Failed to add PlatformerController");
    }
    return EditorOperationResult::success(kPlatformerInvalidation, added(objectTypeId_));
}

EditorOperationResult AddPlatformerControllerCommand::undo(ProjectDocument& document) {
    if (!document.removePlatformerController(objectTypeId_)) {
        return EditorOperationResult::failure("Cannot undo PlatformerController add");
    }
    return EditorOperationResult::success(kPlatformerInvalidation, removed(objectTypeId_));
}

// ----------------------------------------------------------------------------
RemovePlatformerControllerCommand::RemovePlatformerControllerCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult RemovePlatformerControllerCommand::apply(ProjectDocument& document) {
    const PlatformerControllerComponent* current = platformerOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no PlatformerController");
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
    }
    if (!document.removePlatformerController(objectTypeId_)) {
        return EditorOperationResult::failure("Failed to remove PlatformerController");
    }
    return EditorOperationResult::success(kPlatformerInvalidation, removed(objectTypeId_));
}

EditorOperationResult RemovePlatformerControllerCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addPlatformerController(objectTypeId_, removed_)) {
        return EditorOperationResult::failure("Cannot undo PlatformerController removal");
    }
    return EditorOperationResult::success(kPlatformerInvalidation, added(objectTypeId_));
}

// ----------------------------------------------------------------------------
SetPlatformerValueCommand::SetPlatformerValueCommand(std::string objectTypeId,
                                                     PlatformerField field, float value)
    : objectTypeId_(std::move(objectTypeId)), field_(field), next_(value) {}

EditorOperationResult SetPlatformerValueCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(next_) || next_ < 0.f) {
        return EditorOperationResult::failure("PlatformerController value must be >= 0");
    }
    const PlatformerControllerComponent* current = platformerOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no PlatformerController");
    if (readField(*current, field_) == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = readField(*current, field_);
        captured_ = true;
    }
    if (!document.setPlatformerValue(objectTypeId_, static_cast<int>(field_), next_)) {
        return EditorOperationResult::failure("Failed to set PlatformerController value");
    }
    return EditorOperationResult::success(kPlatformerInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetPlatformerValueCommand::undo(ProjectDocument& document) {
    if (!captured_
        || !document.setPlatformerValue(objectTypeId_, static_cast<int>(field_), previous_)) {
        return EditorOperationResult::failure("Cannot undo PlatformerController value change");
    }
    return EditorOperationResult::success(kPlatformerInvalidation, changed(objectTypeId_));
}

} // namespace ArtCade::EditorNative
