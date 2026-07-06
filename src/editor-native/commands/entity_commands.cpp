#include "editor-native/commands/entity_commands.h"

#include "editor-native/model/project_document.h"

#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kPositionInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;
constexpr EditorInvalidation kRenameInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector;
constexpr EditorInvalidation kStructureInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Viewport;
} // namespace

// ----------------------------------------------------------------------------
// CreateEntityCommand
// ----------------------------------------------------------------------------
CreateEntityCommand::CreateEntityCommand(SceneId sceneId, EntityId id,
                                         std::string objectTypeId,
                                         std::string instanceName, Vec2 position,
                                         std::string layerId)
    : sceneId_(std::move(sceneId)), id_(id),
      objectTypeId_(std::move(objectTypeId)),
      instanceName_(std::move(instanceName)), position_(position),
      layerId_(std::move(layerId)) {}

EditorOperationResult CreateEntityCommand::apply(ProjectDocument& document) {
    if (id_ == 0) {
        return EditorOperationResult::failure("Entity id cannot be zero");
    }
    if (objectTypeId_.empty()) {
        return EditorOperationResult::failure("Object type id cannot be empty");
    }
    if (!document.findScene(sceneId_)) {
        return EditorOperationResult::failure("No target scene");
    }
    if (document.findInstanceInScene(sceneId_, id_)) {
        return EditorOperationResult::failure("An instance with that id already exists");
    }
    // An explicit layer must exist in the scene; "" means the scene default.
    if (!layerId_.empty() && !document.hasLayer(sceneId_, layerId_)) {
        return EditorOperationResult::failure("Target layer does not exist in the scene");
    }
    SceneInstanceDef instance;
    instance.id                 = id_;
    instance.objectTypeId       = objectTypeId_;
    instance.instanceName       = instanceName_;
    instance.transform.position = position_;
    instance.layerId            = layerId_;
    if (!document.createInstance(sceneId_, std::move(instance))) {
        return EditorOperationResult::failure("Failed to create instance");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityAdded(sceneId_, id_));
}

EditorOperationResult CreateEntityCommand::undo(ProjectDocument& document) {
    if (!document.deleteInstance(sceneId_, id_)) {
        return EditorOperationResult::failure("Cannot undo instance creation");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityRemoved(sceneId_, id_));
}

// ----------------------------------------------------------------------------
// CreateEntityWithDefaultTypeCommand
// ----------------------------------------------------------------------------
CreateEntityWithDefaultTypeCommand::CreateEntityWithDefaultTypeCommand(
    SceneId sceneId, EntityId id, std::string objectTypeId, std::string objectTypeName,
    std::string instanceName, Vec2 position, std::string layerId)
    : sceneId_(std::move(sceneId)), id_(id),
      objectTypeId_(std::move(objectTypeId)), objectTypeName_(std::move(objectTypeName)),
      instanceName_(std::move(instanceName)), position_(position),
      layerId_(std::move(layerId)) {}

EditorOperationResult CreateEntityWithDefaultTypeCommand::apply(ProjectDocument& document) {
    if (id_ == 0) {
        return EditorOperationResult::failure("Entity id cannot be zero");
    }
    if (objectTypeId_.empty()) {
        return EditorOperationResult::failure("Object type id cannot be empty");
    }
    if (!document.findScene(sceneId_)) {
        return EditorOperationResult::failure("No target scene");
    }
    // Validate both ids up front so the two mutations below cannot fail
    // independently — no partial mutation is possible after this point.
    if (document.hasObjectType(objectTypeId_)) {
        return EditorOperationResult::failure("Object type already exists");
    }
    if (document.findInstanceInScene(sceneId_, id_)) {
        return EditorOperationResult::failure("An instance with that id already exists");
    }
    if (!layerId_.empty() && !document.hasLayer(sceneId_, layerId_)) {
        return EditorOperationResult::failure("Target layer does not exist in the scene");
    }

    EntityDef type;
    type.className = objectTypeId_;   // the catalog key (mirrors load: className == id)
    type.name     = objectTypeName_;
    if (!document.createObjectType(std::move(type))) {
        return EditorOperationResult::failure("Failed to create object type");
    }

    SceneInstanceDef instance;
    instance.id                 = id_;
    instance.objectTypeId       = objectTypeId_;
    instance.instanceName       = instanceName_;
    instance.transform.position = position_;
    instance.layerId            = layerId_;
    if (!document.createInstance(sceneId_, std::move(instance))) {
        document.removeObjectType(objectTypeId_);   // unreachable after validation; no partial state
        return EditorOperationResult::failure("Failed to create instance");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityAdded(sceneId_, id_));
}

EditorOperationResult CreateEntityWithDefaultTypeCommand::undo(ProjectDocument& document) {
    // Exact inverse: drop the instance, then remove precisely the object type
    // this command created.
    if (!document.deleteInstance(sceneId_, id_)) {
        return EditorOperationResult::failure("Cannot undo: instance missing");
    }
    if (!document.removeObjectType(objectTypeId_)) {
        return EditorOperationResult::failure("Cannot undo: object type missing");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityRemoved(sceneId_, id_));
}

// ----------------------------------------------------------------------------
// DeleteEntityCommand
// ----------------------------------------------------------------------------
DeleteEntityCommand::DeleteEntityCommand(SceneId sceneId, EntityId id)
    : sceneId_(std::move(sceneId)), id_(id) {}

EditorOperationResult DeleteEntityCommand::apply(ProjectDocument& document) {
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) {
        return EditorOperationResult::failure("No target scene");
    }
    if (!captured_) {
        bool found = false;
        for (std::size_t i = 0; i < scene->instances.size(); ++i) {
            if (scene->instances[i].id == id_) {
                removed_  = scene->instances[i];
                index_    = i;
                found     = true;
                break;
            }
        }
        if (!found) {
            return EditorOperationResult::failure("No instance with that id in the target scene");
        }
        captured_ = true;
    }
    if (!document.deleteInstance(sceneId_, id_)) {
        return EditorOperationResult::failure("Failed to delete instance");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityRemoved(sceneId_, id_));
}

EditorOperationResult DeleteEntityCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.insertInstance(sceneId_, index_, removed_)) {
        return EditorOperationResult::failure("Cannot undo instance deletion");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityAdded(sceneId_, id_));
}

// ----------------------------------------------------------------------------
// CloneInstanceCommand
// ----------------------------------------------------------------------------
CloneInstanceCommand::CloneInstanceCommand(SceneId sceneId, EntityId sourceId,
                                           EntityId newId, std::string newName,
                                           Vec2 newPosition)
    : sceneId_(std::move(sceneId)), sourceId_(sourceId), newId_(newId),
      newName_(std::move(newName)), newPosition_(newPosition) {}

EditorOperationResult CloneInstanceCommand::apply(ProjectDocument& document) {
    if (newId_ == 0) {
        return EditorOperationResult::failure("Entity id cannot be zero");
    }
    if (newName_.empty()) {
        return EditorOperationResult::failure("Name cannot be empty");
    }
    if (!document.findScene(sceneId_)) {
        return EditorOperationResult::failure("No target scene");
    }
    const SceneInstanceDef* source = document.findInstanceInScene(sceneId_, sourceId_);
    if (!source) {
        return EditorOperationResult::failure("No source instance to clone");
    }
    if (document.findInstanceInScene(sceneId_, newId_)) {
        return EditorOperationResult::failure("An instance with that id already exists");
    }
    SceneInstanceDef clone      = *source;   // transform/visible/layerId/spriteRenderer/
                                             // spriteAnimator/tilemap/localVariableOverrides
                                             // all copied
    clone.id                    = newId_;
    clone.instanceName          = newName_;
    clone.transform.position    = newPosition_;
    if (!document.createInstance(sceneId_, std::move(clone))) {
        return EditorOperationResult::failure("Failed to clone instance");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityAdded(sceneId_, newId_));
}

EditorOperationResult CloneInstanceCommand::undo(ProjectDocument& document) {
    if (!document.deleteInstance(sceneId_, newId_)) {
        return EditorOperationResult::failure("Cannot undo instance clone");
    }
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityRemoved(sceneId_, newId_));
}

// ----------------------------------------------------------------------------
// SetEntityPositionCommand
// ----------------------------------------------------------------------------
SetEntityPositionCommand::SetEntityPositionCommand(SceneId sceneId, EntityId id,
                                                   Vec2 position)
    : sceneId_(std::move(sceneId)), id_(id), newPosition_(position) {}

EditorOperationResult SetEntityPositionCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* current = document.findInstanceInScene(sceneId_, id_);
    if (!current) {
        return EditorOperationResult::failure("No instance with that id in the target scene");
    }
    if (!captured_) {
        oldPosition_ = current->transform.position;
        captured_ = true;
    }
    if (!document.setInstancePosition(sceneId_, id_, newPosition_)) {
        return EditorOperationResult::failure("Failed to set instance position");
    }
    return EditorOperationResult::success(
        kPositionInvalidation, DomainChange::entityChanged(sceneId_, id_));
}

EditorOperationResult SetEntityPositionCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setInstancePosition(sceneId_, id_, oldPosition_)) {
        return EditorOperationResult::failure("Cannot undo position change");
    }
    return EditorOperationResult::success(
        kPositionInvalidation, DomainChange::entityChanged(sceneId_, id_));
}

// ----------------------------------------------------------------------------
// RenameEntityCommand
// ----------------------------------------------------------------------------
RenameEntityCommand::RenameEntityCommand(SceneId sceneId, EntityId id, std::string name)
    : sceneId_(std::move(sceneId)), id_(id), newName_(std::move(name)) {}

EditorOperationResult RenameEntityCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* current = document.findInstanceInScene(sceneId_, id_);
    if (!current) {
        return EditorOperationResult::failure("No instance with that id in the target scene");
    }
    if (newName_.empty()) {
        return EditorOperationResult::failure("Name cannot be empty");
    }
    if (!captured_) {
        oldName_ = current->instanceName;
        captured_ = true;
    }
    if (!document.setInstanceName(sceneId_, id_, newName_)) {
        return EditorOperationResult::failure("Failed to rename instance");
    }
    return EditorOperationResult::success(
        kRenameInvalidation, DomainChange::entityChanged(sceneId_, id_));
}

EditorOperationResult RenameEntityCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setInstanceName(sceneId_, id_, oldName_)) {
        return EditorOperationResult::failure("Cannot undo rename");
    }
    return EditorOperationResult::success(
        kRenameInvalidation, DomainChange::entityChanged(sceneId_, id_));
}

// ----------------------------------------------------------------------------
// RenameObjectTypeCommand
// ----------------------------------------------------------------------------
RenameObjectTypeCommand::RenameObjectTypeCommand(std::string objectTypeId, std::string name)
    : objectTypeId_(std::move(objectTypeId)), newName_(std::move(name)) {}

EditorOperationResult RenameObjectTypeCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type) {
        return EditorOperationResult::failure("No object type with that id");
    }
    if (newName_.empty()) {
        return EditorOperationResult::failure("Name cannot be empty");
    }
    if (type->name == newName_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        oldName_ = type->name;
        captured_ = true;
    }
    if (!document.setObjectTypeName(objectTypeId_, newName_)) {
        return EditorOperationResult::failure("Failed to rename object type");
    }
    return EditorOperationResult::success(kRenameInvalidation, DomainChange::projectChanged());
}

EditorOperationResult RenameObjectTypeCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setObjectTypeName(objectTypeId_, oldName_)) {
        return EditorOperationResult::failure("Cannot undo object type rename");
    }
    return EditorOperationResult::success(kRenameInvalidation, DomainChange::projectChanged());
}

} // namespace ArtCade::EditorNative
