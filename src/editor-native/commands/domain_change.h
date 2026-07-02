#pragma once

#include "core/types.h"

#include <utility>

namespace ArtCade::EditorNative {

enum class DomainChangeKind {
    None,
    ProjectReplaced,
    ProjectChanged,
    SceneAdded,
    SceneRemoved,
    SceneChanged,
    StartSceneChanged,
    EntityAdded,
    EntityRemoved,
    EntityChanged,
    ComponentAdded,
    ComponentRemoved,
    ComponentChanged,
    AssetChanged,
};

enum class ComponentKind {
    None,
    SpriteRenderer,
    SpriteAnimator,
    BoxCollider2D,
    LinearMover,
    TopDownController,
    PlatformerController,
};

struct DomainChange {
    DomainChangeKind kind = DomainChangeKind::None;
    SceneId          sceneId;
    EntityId         entityId = INVALID_ENTITY;
    std::string      objectTypeId;
    ComponentKind    componentKind = ComponentKind::None;
    AssetId          assetId;

    bool isNone() const { return kind == DomainChangeKind::None; }

    static DomainChange none() { return {}; }
    static DomainChange sceneAdded(SceneId scene) {
        DomainChange change;
        change.kind = DomainChangeKind::SceneAdded;
        change.sceneId = std::move(scene);
        return change;
    }
    static DomainChange projectReplaced() {
        DomainChange change;
        change.kind = DomainChangeKind::ProjectReplaced;
        return change;
    }
    static DomainChange projectChanged() {
        DomainChange change;
        change.kind = DomainChangeKind::ProjectChanged;
        return change;
    }
    static DomainChange sceneRemoved(SceneId scene) {
        DomainChange change;
        change.kind = DomainChangeKind::SceneRemoved;
        change.sceneId = std::move(scene);
        return change;
    }
    static DomainChange sceneChanged(SceneId scene) {
        DomainChange change;
        change.kind = DomainChangeKind::SceneChanged;
        change.sceneId = std::move(scene);
        return change;
    }
    static DomainChange startSceneChanged(SceneId scene) {
        DomainChange change;
        change.kind = DomainChangeKind::StartSceneChanged;
        change.sceneId = std::move(scene);
        return change;
    }
    static DomainChange entityChanged(SceneId scene, EntityId entity) {
        DomainChange change;
        change.kind = DomainChangeKind::EntityChanged;
        change.sceneId = std::move(scene);
        change.entityId = entity;
        return change;
    }
    static DomainChange entityAdded(SceneId scene, EntityId entity) {
        DomainChange change;
        change.kind = DomainChangeKind::EntityAdded;
        change.sceneId = std::move(scene);
        change.entityId = entity;
        return change;
    }
    static DomainChange entityRemoved(SceneId scene, EntityId entity) {
        DomainChange change;
        change.kind = DomainChangeKind::EntityRemoved;
        change.sceneId = std::move(scene);
        change.entityId = entity;
        return change;
    }
    static DomainChange componentAdded(SceneId scene, EntityId entity, ComponentKind component) {
        DomainChange change;
        change.kind = DomainChangeKind::ComponentAdded;
        change.sceneId = std::move(scene);
        change.entityId = entity;
        change.componentKind = component;
        return change;
    }
    static DomainChange componentRemoved(SceneId scene, EntityId entity, ComponentKind component) {
        DomainChange change;
        change.kind = DomainChangeKind::ComponentRemoved;
        change.sceneId = std::move(scene);
        change.entityId = entity;
        change.componentKind = component;
        return change;
    }
    static DomainChange componentChanged(SceneId scene, EntityId entity, ComponentKind component) {
        DomainChange change;
        change.kind = DomainChangeKind::ComponentChanged;
        change.sceneId = std::move(scene);
        change.entityId = entity;
        change.componentKind = component;
        return change;
    }
    static DomainChange objectTypeComponentAdded(std::string objectType,
                                                 ComponentKind component) {
        DomainChange change;
        change.kind = DomainChangeKind::ComponentAdded;
        change.objectTypeId = std::move(objectType);
        change.componentKind = component;
        return change;
    }
    static DomainChange objectTypeComponentRemoved(std::string objectType,
                                                   ComponentKind component) {
        DomainChange change;
        change.kind = DomainChangeKind::ComponentRemoved;
        change.objectTypeId = std::move(objectType);
        change.componentKind = component;
        return change;
    }
    static DomainChange objectTypeComponentChanged(std::string objectType,
                                                   ComponentKind component) {
        DomainChange change;
        change.kind = DomainChangeKind::ComponentChanged;
        change.objectTypeId = std::move(objectType);
        change.componentKind = component;
        return change;
    }
    static DomainChange assetChanged(AssetId asset) {
        DomainChange change;
        change.kind = DomainChangeKind::AssetChanged;
        change.assetId = std::move(asset);
        return change;
    }
};

} // namespace ArtCade::EditorNative
