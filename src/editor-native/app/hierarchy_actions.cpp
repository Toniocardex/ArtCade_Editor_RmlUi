#include "editor-native/app/hierarchy_actions.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/model/project_document.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace ArtCade::EditorNative {

namespace {

// A real, unique object-type id for a newly created entity. The id is a stable
// token ("object-N"), never the display name — a visual name must not double as
// an identifier.
std::string makeUniqueObjectTypeId(const ProjectDocument& document) {
    for (int n = 1;; ++n) {
        std::string candidate = "object-" + std::to_string(n);
        if (!document.hasObjectType(candidate)) return candidate;
    }
}

// The layer new entities go into: the workspace active layer when it is a real
// layer of this scene, otherwise the scene's persistent default ("" for a legacy
// scene with no layers). The caller passes this explicitly to the command.
std::string activeLayerFor(const EditorCoordinator& coordinator, const SceneId& sceneId) {
    const std::string& active = coordinator.sceneView(sceneId).activeLayerId;
    if (!active.empty() && coordinator.document().hasLayer(sceneId, active)) return active;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    return scene ? scene->defaultLayerId : std::string{};
}

// A scene-unique display name from @p base: "base", then "base 2", "base 3", ...
std::string makeUniqueInstanceName(const SceneDef& scene, const std::string& base) {
    const auto taken = [&](const std::string& name) {
        for (const SceneInstanceDef& inst : scene.instances)
            if (inst.instanceName == name) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int n = 2;; ++n) {
        std::string candidate = base + " " + std::to_string(n);
        if (!taken(candidate)) return candidate;
    }
}

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float snapAxis(float value, float gridSize) {
    return std::round(value / gridSize) * gridSize;
}

float clampSpawnAxis(float value, float size, float margin) {
    const float safeSize = std::max(0.0f, finiteOr(size, 0.0f));
    const float safeMargin = std::max(0.0f, finiteOr(margin, 0.0f));
    const float low = std::min(safeMargin, safeSize * 0.5f);
    const float high = std::max(low, safeSize - low);
    return std::clamp(finiteOr(value, low), low, high);
}

} // namespace

EntityId nextAvailableEntityId(const ProjectDocument& document, const SceneId& sceneId) {
    EntityId maxId = 0;
    if (const SceneDef* scene = document.findScene(sceneId)) {
        for (const SceneInstanceDef& inst : scene->instances) {
            if (inst.id > maxId) maxId = inst.id;
        }
    }
    return maxId + 1;   // >= 1, so never INVALID_ENTITY
}

SceneId makeUniqueSceneId(const ProjectDocument& document) {
    for (int n = 1;; ++n) {
        SceneId candidate = "scene-" + std::to_string(n);
        if (!document.hasScene(candidate)) return candidate;
    }
}

Vec2 normalizeSpawnPosition(Vec2 worldPosition, Vec2 sceneSize,
                            SpawnPositionOptions options) {
    Vec2 out = worldPosition;
    if (options.snapToGrid && std::isfinite(options.gridSize) && options.gridSize > 0.0f) {
        out.x = snapAxis(out.x, options.gridSize);
        out.y = snapAxis(out.y, options.gridSize);
    }
    out.x = clampSpawnAxis(out.x, sceneSize.x, options.edgeMargin);
    out.y = clampSpawnAxis(out.y, sceneSize.y, options.edgeMargin);
    return out;
}

Vec2 defaultSpawnPosition(const ViewportRect& viewport,
                          const EditorSceneViewState& view,
                          Vec2 sceneSize,
                          SpawnPositionOptions options) {
    const SceneViewCamera camera = makeSceneViewCamera(viewport, view, sceneSize);
    const Vec2 screenCenter{
        static_cast<float>(viewport.x) + static_cast<float>(viewport.width) * 0.5f,
        static_cast<float>(viewport.y) + static_cast<float>(viewport.height) * 0.5f,
    };
    return normalizeSpawnPosition(screenToWorld(camera, screenCenter), sceneSize, options);
}

EditorOperationResult addScene(EditorCoordinator& coordinator) {
    const SceneId id = makeUniqueSceneId(coordinator.document());
    // Display name mirrors the id's ordinal: "scene-3" -> "Scene 3".
    const std::string name = "Scene " + id.substr(std::string("scene-").size());
    return coordinator.execute(CreateSceneCommand{id, name});
}

EditorOperationResult deleteScene(EditorCoordinator& coordinator, const SceneId& sceneId) {
    if (!coordinator.document().hasScene(sceneId)) {
        return EditorOperationResult::failure("No scene to delete");
    }
    return coordinator.execute(DeleteSceneCommand{sceneId});
}

EditorOperationResult setStartScene(EditorCoordinator& coordinator, const SceneId& sceneId) {
    if (!coordinator.document().hasScene(sceneId)) {
        return EditorOperationResult::failure("No scene to set as start");
    }
    return coordinator.execute(SetStartSceneCommand{sceneId});
}

EditorOperationResult addEntityAt(EditorCoordinator& coordinator, Vec2 spawnPosition) {
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (sceneId.empty() || !scene) {
        return EditorOperationResult::failure("No active scene to add an entity to");
    }
    // "+Entity" creates an independent object: always a NEW object type plus its
    // first instance — never a reuse of an existing type. Reusing a type (placing
    // another instance that intentionally shares its components) is a separate
    // "Add Instance" operation. Because BoxCollider2D, LinearMover
    // and TopDownController are object-type-owned, a fresh EntityId alone would not
    // make the components independent; a fresh ObjectTypeId is required.
    const EntityId id = nextAvailableEntityId(coordinator.document(), sceneId);
    const std::string instanceName = "Entity " + std::to_string(id);
    const std::string objectTypeId = makeUniqueObjectTypeId(coordinator.document());
    return coordinator.execute(CreateEntityWithDefaultTypeCommand{
        sceneId, id, objectTypeId, /*objectTypeName*/ "Entity", instanceName,
        spawnPosition, /*layerId*/ activeLayerFor(coordinator, sceneId)});
}

EditorOperationResult addEntity(EditorCoordinator& coordinator) {
    const SceneDef* scene = coordinator.document().findScene(coordinator.state().activeSceneId);
    if (!scene) return EditorOperationResult::failure("No active scene to add an entity to");
    return addEntityAt(
        coordinator,
        normalizeSpawnPosition(
            Vec2{scene->worldSize.x * 0.5f, scene->worldSize.y * 0.5f},
            scene->worldSize));
}

EditorOperationResult addInstanceOfSelectedTypeAt(EditorCoordinator& coordinator,
                                                  Vec2 spawnPosition) {
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (sceneId.empty() || !scene) {
        return EditorOperationResult::failure("No active scene to add an instance to");
    }
    const EntityId selected = coordinator.selection().primaryEntity;
    const SceneInstanceDef* source =
        coordinator.document().findInstanceInScene(sceneId, selected);
    if (!source) {
        return EditorOperationResult::failure("Select an entity to instance its type");
    }
    const std::string objectTypeId = source->objectTypeId;
    // The chosen type must really exist in the catalog (no "Entity" fallback, no
    // first-available guess); otherwise fail without mutating anything.
    const EntityDef* type = coordinator.document().findObjectType(objectTypeId);
    if (!type) {
        return EditorOperationResult::failure("Selected entity has no object type");
    }
    const EntityId id = nextAvailableEntityId(coordinator.document(), sceneId);
    const std::string name =
        makeUniqueInstanceName(*coordinator.document().findScene(sceneId), type->name);
    // Reuse the existing structural command — a new instance bound to the existing
    // type (shared components, no ObjectTypeDef duplicated).
    const EditorOperationResult result =
        coordinator.execute(CreateEntityCommand{
            sceneId, id, objectTypeId, name, spawnPosition,
            /*layerId*/ activeLayerFor(coordinator, sceneId)});
    // Select the new instance — workspace state, not an undo-history entry.
    if (result.ok) coordinator.apply(SelectEntityIntent{id});
    return result;
}

EditorOperationResult addInstanceOfSelectedType(EditorCoordinator& coordinator) {
    const SceneDef* scene = coordinator.document().findScene(coordinator.state().activeSceneId);
    if (!scene) return EditorOperationResult::failure("No active scene to add an instance to");
    return addInstanceOfSelectedTypeAt(
        coordinator,
        normalizeSpawnPosition(
            Vec2{scene->worldSize.x * 0.5f, scene->worldSize.y * 0.5f},
            scene->worldSize));
}

EditorOperationResult deleteSelectedEntity(EditorCoordinator& coordinator) {
    const EditorState& state = coordinator.state();
    if (state.activeSceneId.empty() || !state.selection.hasEntity()) {
        return EditorOperationResult::failure("No selected entity to delete");
    }
    return coordinator.execute(
        DeleteEntityCommand{state.activeSceneId, state.selection.primaryEntity});
}

} // namespace ArtCade::EditorNative
