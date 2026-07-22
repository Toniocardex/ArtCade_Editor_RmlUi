#include "editor-native/commands/auto_destroy_commands.h"

#include "editor-native/model/project_document.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

constexpr EditorInvalidation kAutoDestroyInvalidation = EditorInvalidation::Inspector;
constexpr float kDefaultAutoDestroyLifespanSeconds = 1.f;

const AutoDestroyComponent* autoDestroyOf(const ProjectDocument& document,
                                          const std::string& objectTypeId) {
    const EntityDef* type = document.findObjectType(objectTypeId);
    return (type && type->autoDestroy) ? &*type->autoDestroy : nullptr;
}

DomainChange added(const std::string& objectTypeId) {
    return DomainChange::objectTypeComponentAdded(objectTypeId, ComponentKind::AutoDestroy);
}

DomainChange removed(const std::string& objectTypeId) {
    return DomainChange::objectTypeComponentRemoved(objectTypeId, ComponentKind::AutoDestroy);
}

DomainChange changed(const std::string& objectTypeId) {
    return DomainChange::objectTypeComponentChanged(objectTypeId, ComponentKind::AutoDestroy);
}

} // namespace

AddAutoDestroyCommand::AddAutoDestroyCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult AddAutoDestroyCommand::apply(ProjectDocument& document) {
    if (!document.findObjectType(objectTypeId_)) {
        return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);
    }
    if (autoDestroyOf(document, objectTypeId_)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    AutoDestroyComponent component;
    component.lifespan = kDefaultAutoDestroyLifespanSeconds;
    if (!document.addAutoDestroy(objectTypeId_, component)) {
        return EditorOperationResult::failure("Failed to add AutoDestroy");
    }
    return EditorOperationResult::success(kAutoDestroyInvalidation, added(objectTypeId_));
}

EditorOperationResult AddAutoDestroyCommand::undo(ProjectDocument& document) {
    if (!document.removeAutoDestroy(objectTypeId_)) {
        return EditorOperationResult::failure("Cannot undo AutoDestroy add");
    }
    return EditorOperationResult::success(kAutoDestroyInvalidation, removed(objectTypeId_));
}

RemoveAutoDestroyCommand::RemoveAutoDestroyCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult RemoveAutoDestroyCommand::apply(ProjectDocument& document) {
    const AutoDestroyComponent* current = autoDestroyOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no AutoDestroy");
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
    }
    if (!document.removeAutoDestroy(objectTypeId_)) {
        return EditorOperationResult::failure("Failed to remove AutoDestroy");
    }
    return EditorOperationResult::success(kAutoDestroyInvalidation, removed(objectTypeId_));
}

EditorOperationResult RemoveAutoDestroyCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addAutoDestroy(objectTypeId_, removed_)) {
        return EditorOperationResult::failure("Cannot undo AutoDestroy removal");
    }
    return EditorOperationResult::success(kAutoDestroyInvalidation, added(objectTypeId_));
}

SetAutoDestroyLifespanCommand::SetAutoDestroyLifespanCommand(std::string objectTypeId,
                                                               float lifespan)
    : objectTypeId_(std::move(objectTypeId)), next_(lifespan) {}

EditorOperationResult SetAutoDestroyLifespanCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(next_) || next_ < 0.f) {
        return EditorOperationResult::failure("AutoDestroy lifetime must be >= 0");
    }
    const AutoDestroyComponent* current = autoDestroyOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no AutoDestroy");
    if (current->lifespan == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = current->lifespan;
        captured_ = true;
    }
    if (!document.setAutoDestroyLifespan(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set AutoDestroy lifetime");
    }
    return EditorOperationResult::success(kAutoDestroyInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetAutoDestroyLifespanCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setAutoDestroyLifespan(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo AutoDestroy lifetime change");
    }
    return EditorOperationResult::success(kAutoDestroyInvalidation, changed(objectTypeId_));
}

} // namespace ArtCade::EditorNative
