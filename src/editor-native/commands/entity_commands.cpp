#include "editor-native/commands/entity_commands.h"
#include "editor-native/model/numeric_validation.h"

#include "editor-native/model/project_document.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kPositionInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;
constexpr EditorInvalidation kRenameInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector;
// Inspector included: the Scene Inspector renders structural facts (the
// Entities count, the Outside-bounds diagnostic), so an instance created or
// removed while no entity is selected must refresh it too - otherwise the
// shown count goes stale until the next unrelated Inspector invalidation.
constexpr EditorInvalidation kStructureInvalidation =
    EditorInvalidation::Hierarchy | EditorInvalidation::Inspector
    | EditorInvalidation::Viewport;
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
    if (!NumericValidation::isFinite(position_)) {
        return EditorOperationResult::failure("Entity position must be finite");
    }
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) {
        return EditorOperationResult::failure("No target scene");
    }
    if (document.findInstanceInScene(sceneId_, id_)) {
        return EditorOperationResult::failure("An instance with that id already exists");
    }
    // An explicit layer must exist in the scene; "" means the scene default.
    if (!layerId_.empty() && !document.hasLayer(sceneId_, layerId_)) {
        return EditorOperationResult::failure("Target layer does not exist in the scene");
    }
    // "" means the scene default - resolved here, not left on the instance:
    // the canonical project format (validate_current_project_document)
    // requires every instance to carry a real, non-empty layerId.
    const std::string targetLayer = layerId_.empty() ? scene->defaultLayerId : layerId_;
    if (!captured_) {
        // Gates only the first apply() (the new user gesture) - a later redo
        // reuses this same command and must not be blocked by the layer's
        // lock state at redo time.
        if (document.isLayerLocked(sceneId_, targetLayer)) {
            return EditorOperationResult::failure("Cannot create instance: target layer is locked");
        }
        captured_ = true;
    }
    SceneInstanceDef instance;
    instance.id                 = id_;
    instance.objectTypeId       = objectTypeId_;
    instance.instanceName       = instanceName_;
    instance.transform.position = position_;
    instance.layerId            = targetLayer;
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
    if (!NumericValidation::isFinite(position_)) {
        return EditorOperationResult::failure("Entity position must be finite");
    }
    const SceneDef* scene = document.findScene(sceneId_);
    if (!scene) {
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
    // "" means the scene default - resolved here, not left on the instance:
    // the canonical project format (validate_current_project_document)
    // requires every instance to carry a real, non-empty layerId.
    const std::string targetLayer = layerId_.empty() ? scene->defaultLayerId : layerId_;
    if (!captured_) {
        if (document.isLayerLocked(sceneId_, targetLayer)) {
            return EditorOperationResult::failure("Cannot create instance: target layer is locked");
        }
        captured_ = true;
    }

    EntityDef type;
    type.className = objectTypeId_;   // the catalog key (mirrors load: className == id)
    type.name     = objectTypeName_;
    // Placeholder fill for a type with no artwork yet: neutral zinc instead of
    // the struct's white - white placeholders disappear on light scene
    // backgrounds and blind on the new dark default. Authoring data: the
    // Inspector can recolor it, and types from saved files keep their own.
    type.sprite.fillColor = Vec3{0.42f, 0.45f, 0.52f};
    SceneInstanceDef instance;
    instance.id                 = id_;
    instance.objectTypeId       = objectTypeId_;
    instance.instanceName       = instanceName_;
    instance.transform.position = position_;
    instance.layerId            = targetLayer;

    ProjectDoc staged = document.data();
    auto stagedScene = staged.scenes.find(sceneId_);
    if (stagedScene == staged.scenes.end()) {
        return EditorOperationResult::failure("Failed to stage target scene");
    }
    staged.objectTypes.emplace(objectTypeId_, std::move(type));
    stagedScene->second.instances.push_back(std::move(instance));
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kStructureInvalidation,
                                          DomainChange::entityAdded(sceneId_, id_));
}

EditorOperationResult CreateEntityWithDefaultTypeCommand::undo(ProjectDocument& document) {
    if (!captured_) {
        return EditorOperationResult::failure("Cannot undo: command state was not captured");
    }
    ProjectDoc staged = document.data();
    auto sceneIt = staged.scenes.find(sceneId_);
    if (sceneIt == staged.scenes.end()) {
        return EditorOperationResult::failure("Cannot undo: instance missing");
    }
    auto instanceIt = std::find_if(
        sceneIt->second.instances.begin(), sceneIt->second.instances.end(),
        [&](const SceneInstanceDef& instance) { return instance.id == id_; });
    if (instanceIt == sceneIt->second.instances.end()
        || instanceIt->objectTypeId != objectTypeId_) {
        return EditorOperationResult::failure("Cannot undo: instance missing");
    }
    if (staged.objectTypes.find(objectTypeId_) == staged.objectTypes.end()) {
        return EditorOperationResult::failure("Cannot undo: object type missing");
    }
    for (const auto& [otherSceneId, scene] : staged.scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            const bool isCreatedInstance = otherSceneId == sceneId_ && instance.id == id_;
            if (!isCreatedInstance && instance.objectTypeId == objectTypeId_) {
                return EditorOperationResult::failure("Cannot undo: object type is still referenced");
            }
        }
    }
    sceneIt->second.instances.erase(instanceIt);
    staged.objectTypes.erase(objectTypeId_);
    document.commitStagedCommand(std::move(staged));
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
                if (document.isInstanceLayerLocked(sceneId_, scene->instances[i])) {
                    return EditorOperationResult::failure("Cannot delete \""
                        + scene->instances[i].instanceName + "\": its layer is locked");
                }
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
    if (!NumericValidation::isFinite(newPosition_)) {
        return EditorOperationResult::failure("Entity position must be finite");
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
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *source)) {
            return EditorOperationResult::failure("Cannot duplicate \"" + source->instanceName
                + "\": its layer is locked");
        }
        captured_ = true;
    }
    SceneInstanceDef clone      = *source;   // transform/visible/layerId, sparse
                                             // presentation overrides, tilemap and
                                             // localVariableOverrides are copied
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
// SetEntityTransformCommand
// ----------------------------------------------------------------------------
SetEntityTransformCommand::SetEntityTransformCommand(SceneId sceneId, EntityId id,
                                                     AuthoredTransformPatch patch)
    : sceneId_(std::move(sceneId)), id_(id), patch_(std::move(patch)) {}

EditorOperationResult SetEntityTransformCommand::apply(ProjectDocument& document) {
    if (patch_.position && !NumericValidation::isFinite(*patch_.position)) {
        return EditorOperationResult::failure("Entity position must be finite");
    }
    if (patch_.rotationRadians && !NumericValidation::isFinite(*patch_.rotationRadians)) {
        return EditorOperationResult::failure("Entity rotation must be finite");
    }
    if (patch_.scale) {
        if (!NumericValidation::isFinite(*patch_.scale)
            || patch_.scale->x < kMinAuthoringScale
            || patch_.scale->y < kMinAuthoringScale) {
            return EditorOperationResult::failure(
                "Entity scale must be finite and at least "
                + formatAuthoringFloat(kMinAuthoringScale));
        }
    }
    if (!patch_.position && !patch_.rotationRadians && !patch_.scale) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }

    const SceneInstanceDef* current = document.findInstanceInScene(sceneId_, id_);
    if (!current) {
        return EditorOperationResult::failure("No instance with that id in the target scene");
    }

    const Transform& xf = current->transform;
    const Vec2 nextPosition = patch_.position.value_or(xf.position);
    const float nextRotation = patch_.rotationRadians.value_or(xf.rotation);
    const Vec2 nextScale = patch_.scale.value_or(xf.scale);
    if (nearlyEqualTransform(nextPosition, xf.position)
        && nearlyEqualTransform(nextRotation, xf.rotation)
        && nearlyEqualTransform(nextScale, xf.scale)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }

    if (!captured_) {
        // The lock gate only guards the new user gesture that first calls
        // apply() - a later redo reuses this same command with captured_
        // already true and must never be blocked by the layer's current
        // lock state (undo/redo must always be reproducible).
        if (document.isInstanceLayerLocked(sceneId_, *current)) {
            return EditorOperationResult::failure("Cannot transform \"" + current->instanceName
                + "\": its layer is locked");
        }
        if (patch_.position) undoPatch_.position = xf.position;
        if (patch_.rotationRadians) undoPatch_.rotationRadians = xf.rotation;
        if (patch_.scale) undoPatch_.scale = xf.scale;
        captured_ = true;
    }
    if (!document.patchInstanceTransform(sceneId_, id_, patch_)) {
        return EditorOperationResult::failure("Failed to patch instance transform");
    }
    return EditorOperationResult::success(
        kPositionInvalidation, DomainChange::entityChanged(sceneId_, id_));
}

EditorOperationResult SetEntityTransformCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.patchInstanceTransform(sceneId_, id_, undoPatch_)) {
        return EditorOperationResult::failure("Cannot undo transform change");
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
        if (document.isInstanceLayerLocked(sceneId_, *current)) {
            return EditorOperationResult::failure("Cannot rename \"" + current->instanceName
                + "\": its layer is locked");
        }
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
// SetInstanceVisibleCommand
// ----------------------------------------------------------------------------
SetInstanceVisibleCommand::SetInstanceVisibleCommand(
    SceneId sceneId, EntityId id, bool visible)
    : sceneId_(std::move(sceneId)), id_(id), next_(visible) {}

EditorOperationResult SetInstanceVisibleCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* current = document.findInstanceInScene(sceneId_, id_);
    if (!current) {
        return EditorOperationResult::failure(
            "No instance with that id in the target scene");
    }
    if (current->visible == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *current)) {
            return EditorOperationResult::failure(
                "Cannot change instance visibility: layer is locked");
        }
        previous_ = current->visible;
        captured_ = true;
    }
    if (!document.setInstanceVisible(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set instance visibility");
    }
    return EditorOperationResult::success(
        kPositionInvalidation, DomainChange::entityChanged(sceneId_, id_));
}

EditorOperationResult SetInstanceVisibleCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setInstanceVisible(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo instance visibility change");
    }
    return EditorOperationResult::success(
        kPositionInvalidation, DomainChange::entityChanged(sceneId_, id_));
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
