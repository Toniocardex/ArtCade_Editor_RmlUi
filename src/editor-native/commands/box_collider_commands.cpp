#include "editor-native/commands/box_collider_commands.h"

#include "editor-native/model/project_document.h"
#include "core/box-collider-resolve.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kBoxInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

bool finite(Vec2 v) {
    return std::isfinite(v.x) && std::isfinite(v.y);
}

bool positive(Vec2 v) {
    return v.x > 0.f && v.y > 0.f;
}

const EntityDef* objectTypeOf(const ProjectDocument& document, const std::string& id) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(id);
    return it == types.end() ? nullptr : &it->second;
}

const BoxCollider2DComponent* colliderOf(const ProjectDocument& document,
                                         const std::string& id) {
    const EntityDef* type = objectTypeOf(document, id);
    return (type && type->boxCollider2D) ? &*type->boxCollider2D : nullptr;
}

DomainChange added(const std::string& id) {
    return DomainChange::objectTypeComponentAdded(id, ComponentKind::BoxCollider2D);
}
DomainChange removed(const std::string& id) {
    return DomainChange::objectTypeComponentRemoved(id, ComponentKind::BoxCollider2D);
}
DomainChange changed(const std::string& id) {
    return DomainChange::objectTypeComponentChanged(id, ComponentKind::BoxCollider2D);
}
} // namespace

AddBoxColliderCommand::AddBoxColliderCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult AddBoxColliderCommand::apply(ProjectDocument& document) {
    const EntityDef* type = objectTypeOf(document, objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);
    if (type->boxCollider2D.has_value()) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!document.addBoxCollider(objectTypeId_, BoxCollider2DComponent{})) {
        return EditorOperationResult::failure("Failed to add BoxCollider2D");
    }
    return EditorOperationResult::success(kBoxInvalidation, added(objectTypeId_));
}

EditorOperationResult AddBoxColliderCommand::undo(ProjectDocument& document) {
    if (!document.removeBoxCollider(objectTypeId_)) {
        return EditorOperationResult::failure("Cannot undo BoxCollider2D add");
    }
    return EditorOperationResult::success(kBoxInvalidation, removed(objectTypeId_));
}

RemoveBoxColliderCommand::RemoveBoxColliderCommand(std::string objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult RemoveBoxColliderCommand::apply(ProjectDocument& document) {
    const BoxCollider2DComponent* current = colliderOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no BoxCollider2D");
    if (!captured_) {
        removed_ = *current;
        for (const auto& [sceneId, scene] : document.data().scenes) {
            for (const SceneInstanceDef& instance : scene.instances) {
                if (instance.objectTypeId == objectTypeId_
                    && instance.boxCollider2DOverride) {
                    removedOverrides_.emplace_back(
                        sceneId, instance.id, *instance.boxCollider2DOverride);
                }
            }
        }
        captured_ = true;
    }
    if (!document.removeBoxCollider(objectTypeId_)) {
        return EditorOperationResult::failure("Failed to remove BoxCollider2D");
    }
    return EditorOperationResult::success(kBoxInvalidation, removed(objectTypeId_));
}

EditorOperationResult RemoveBoxColliderCommand::undo(ProjectDocument& document) {
    if (!captured_
        || !document.restoreBoxColliderAndOverrides(
            objectTypeId_, removed_, removedOverrides_)) {
        return EditorOperationResult::failure("Cannot undo BoxCollider2D removal");
    }
    return EditorOperationResult::success(kBoxInvalidation, added(objectTypeId_));
}

SetInstanceOriginFromColliderAnchorCommand::SetInstanceOriginFromColliderAnchorCommand(
    SceneId sceneId, EntityId entityId,
    ColliderAnchorX anchorX, ColliderAnchorY anchorY)
    : sceneId_(std::move(sceneId)), entityId_(entityId),
      anchorX_(anchorX), anchorY_(anchorY) {}

EditorOperationResult SetInstanceOriginFromColliderAnchorCommand::apply(
    ProjectDocument& document) {
    const SceneInstanceDef* instance =
        document.findInstanceInScene(sceneId_, entityId_);
    if (!instance) {
        return EditorOperationResult::failure("No instance with that id in the target scene");
    }
    const EntityDef* type = document.findObjectType(instance->objectTypeId);
    if (!type) return EditorOperationResult::failure("Instance Object Type is missing");
    const EffectiveBoxCollider2D collider =
        resolveEffectiveBoxCollider2D(*type, *instance);
    if (!collider.present || !collider.value.enabled) {
        return EditorOperationResult::failure("Instance has no enabled BoxCollider2D");
    }

    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *instance)) {
            return EditorOperationResult::failure(
                "Cannot edit Entity Origin: its layer is locked");
        }
        const std::optional<ColliderOriginEdit> edit = originEditForColliderAnchor(
            instance->transform, collider.value, anchorX_, anchorY_);
        if (!edit) return EditorOperationResult::failure("Cannot resolve collider anchor");
        previousPosition_ = instance->transform.position;
        previousOverride_ = instance->boxCollider2DOverride;
        nextPosition_ = edit->position;
        nextOverride_ = BoxCollider2DOverride{edit->offset};
        // Normalize away a redundant per-instance value.
        if (type->boxCollider2D
            && type->boxCollider2D->offset.x == edit->offset.x
            && type->boxCollider2D->offset.y == edit->offset.y) {
            nextOverride_.reset();
        }
        captured_ = true;
    }

    const bool samePosition = instance->transform.position.x == nextPosition_.x
        && instance->transform.position.y == nextPosition_.y;
    const auto offsetOf = [](const std::optional<BoxCollider2DOverride>& value)
        -> std::optional<Vec2> {
        return value ? value->offset : std::nullopt;
    };
    const std::optional<Vec2> currentOffset = offsetOf(instance->boxCollider2DOverride);
    const std::optional<Vec2> desiredOffset = offsetOf(nextOverride_);
    const bool sameOffset = (!currentOffset && !desiredOffset)
        || (currentOffset && desiredOffset
            && currentOffset->x == desiredOffset->x
            && currentOffset->y == desiredOffset->y);
    if (samePosition && sameOffset) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!document.setInstanceOriginAndBoxColliderOverride(
            sceneId_, entityId_, nextPosition_, nextOverride_)) {
        return EditorOperationResult::failure("Failed to edit Entity Origin");
    }
    return EditorOperationResult::success(
        kBoxInvalidation, DomainChange::entityChanged(sceneId_, entityId_));
}

EditorOperationResult SetInstanceOriginFromColliderAnchorCommand::undo(
    ProjectDocument& document) {
    if (!captured_ || !document.setInstanceOriginAndBoxColliderOverride(
            sceneId_, entityId_, previousPosition_, previousOverride_)) {
        return EditorOperationResult::failure("Cannot undo Entity Origin edit");
    }
    return EditorOperationResult::success(
        kBoxInvalidation, DomainChange::entityChanged(sceneId_, entityId_));
}

SetInstanceBoxColliderOffsetOverrideCommand::SetInstanceBoxColliderOffsetOverrideCommand(
    SceneId sceneId, EntityId entityId, std::optional<Vec2> offset)
    : sceneId_(std::move(sceneId)), entityId_(entityId) {
    if (offset) next_ = BoxCollider2DOverride{*offset};
}

EditorOperationResult SetInstanceBoxColliderOffsetOverrideCommand::apply(
    ProjectDocument& document) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId_, entityId_);
    if (!instance) return EditorOperationResult::failure("No selected instance");
    const EntityDef* type = document.findObjectType(instance->objectTypeId);
    if (!type || !type->boxCollider2D) {
        return EditorOperationResult::failure("Instance has no BoxCollider2D");
    }
    const auto offsetOf = [](const std::optional<BoxCollider2DOverride>& value)
        -> std::optional<Vec2> { return value ? value->offset : std::nullopt; };
    const std::optional<Vec2> current = offsetOf(instance->boxCollider2DOverride);
    const std::optional<Vec2> desired = offsetOf(next_);
    if ((!current && !desired) || (current && desired
        && current->x == desired->x && current->y == desired->y)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *instance)) {
            return EditorOperationResult::failure(
                "Cannot edit collider alignment: its layer is locked");
        }
        previous_ = instance->boxCollider2DOverride;
        captured_ = true;
    }
    if (!document.setInstanceOriginAndBoxColliderOverride(
            sceneId_, entityId_, instance->transform.position, next_)) {
        return EditorOperationResult::failure("Failed to edit collider alignment");
    }
    return EditorOperationResult::success(
        kBoxInvalidation, DomainChange::entityChanged(sceneId_, entityId_));
}

EditorOperationResult SetInstanceBoxColliderOffsetOverrideCommand::undo(
    ProjectDocument& document) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId_, entityId_);
    if (!captured_ || !instance || !document.setInstanceOriginAndBoxColliderOverride(
            sceneId_, entityId_, instance->transform.position, previous_)) {
        return EditorOperationResult::failure("Cannot undo collider alignment edit");
    }
    return EditorOperationResult::success(
        kBoxInvalidation, DomainChange::entityChanged(sceneId_, entityId_));
}

SetBoxColliderOffsetCommand::SetBoxColliderOffsetCommand(std::string objectTypeId, Vec2 offset)
    : objectTypeId_(std::move(objectTypeId)), next_(offset) {}

EditorOperationResult SetBoxColliderOffsetCommand::apply(ProjectDocument& document) {
    if (!finite(next_)) return EditorOperationResult::failure("BoxCollider2D offset is invalid");
    const BoxCollider2DComponent* current = colliderOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no BoxCollider2D");
    if (current->offset.x == next_.x && current->offset.y == next_.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = current->offset;
        captured_ = true;
    }
    if (!document.setBoxColliderOffset(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set BoxCollider2D offset");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetBoxColliderOffsetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setBoxColliderOffset(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo BoxCollider2D offset change");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

SetBoxColliderSizeCommand::SetBoxColliderSizeCommand(std::string objectTypeId, Vec2 size)
    : objectTypeId_(std::move(objectTypeId)), next_(size) {}

EditorOperationResult SetBoxColliderSizeCommand::apply(ProjectDocument& document) {
    if (!finite(next_) || !positive(next_)) {
        return EditorOperationResult::failure("BoxCollider2D size must be positive");
    }
    const BoxCollider2DComponent* current = colliderOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no BoxCollider2D");
    if (current->size.x == next_.x && current->size.y == next_.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previous_ = current->size;
        captured_ = true;
    }
    if (!document.setBoxColliderSize(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set BoxCollider2D size");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetBoxColliderSizeCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setBoxColliderSize(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo BoxCollider2D size change");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

SetBoxColliderEnabledCommand::SetBoxColliderEnabledCommand(std::string objectTypeId, bool enabled)
    : objectTypeId_(std::move(objectTypeId)), next_(enabled) {}

EditorOperationResult SetBoxColliderEnabledCommand::apply(ProjectDocument& document) {
    const BoxCollider2DComponent* current = colliderOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no BoxCollider2D");
    if (current->enabled == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = current->enabled;
        captured_ = true;
    }
    if (!document.setBoxColliderEnabled(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set BoxCollider2D enabled");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetBoxColliderEnabledCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setBoxColliderEnabled(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo BoxCollider2D enabled change");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

SetBoxColliderModeCommand::SetBoxColliderModeCommand(std::string objectTypeId,
                                                     BoxColliderMode mode)
    : objectTypeId_(std::move(objectTypeId)), next_(mode) {}

EditorOperationResult SetBoxColliderModeCommand::apply(ProjectDocument& document) {
    const EntityDef* type = objectTypeOf(document, objectTypeId_);
    const BoxCollider2DComponent* current = colliderOf(document, objectTypeId_);
    if (!current) return EditorOperationResult::failure("Object type has no BoxCollider2D");
    if (current->mode == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (next_ == BoxColliderMode::OneWayPlatform
        && type
        && (type->linearMover || type->topDownController || type->platformerController)) {
        return EditorOperationResult::failure(
            "OneWayPlatform does not support movement drivers");
    }
    if (!captured_) {
        previous_ = current->mode;
        captured_ = true;
    }
    if (!document.setBoxColliderMode(objectTypeId_, next_)) {
        return EditorOperationResult::failure("Failed to set BoxCollider2D mode");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

EditorOperationResult SetBoxColliderModeCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setBoxColliderMode(objectTypeId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo BoxCollider2D mode change");
    }
    return EditorOperationResult::success(kBoxInvalidation, changed(objectTypeId_));
}

} // namespace ArtCade::EditorNative
