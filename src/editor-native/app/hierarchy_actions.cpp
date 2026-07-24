#include "editor-native/app/hierarchy_actions.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/instance_name_policy.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/scene_commands.h"
#include "editor-native/commands/tilemap_commands.h"
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

// How far a duplicate is nudged from its source: visible but modest, half the
// default grid cell size, so it never lands exactly on top of the source.
constexpr float kDuplicateOffset = 24.0f;

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

Vec2 unoccupiedSpawnPosition(const SceneDef& scene, Vec2 candidate, Vec2 sceneSize,
                             SpawnPositionOptions options) {
    // Half a grid cell per step, the same visible-but-modest offset a clone
    // gets (kDuplicateOffset): enough that labels never overlap exactly.
    const float step = (std::isfinite(options.gridSize) && options.gridSize > 0.0f)
        ? options.gridSize * 0.5f
        : kDuplicateOffset;
    const auto occupiedAt = [&](Vec2 point) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (std::fabs(instance.transform.position.x - point.x) < step * 0.5f
                && std::fabs(instance.transform.position.y - point.y) < step * 0.5f) {
                return true;
            }
        }
        return false;
    };
    Vec2 out = candidate;
    for (int i = 0; i < 8 && occupiedAt(out); ++i) {
        const Vec2 next =
            normalizeSpawnPosition(Vec2{out.x + step, out.y + step}, sceneSize, options);
        if (next.x == out.x && next.y == out.y) break;   // clamped into the corner
        out = next;
    }
    return out;
}

EditorOperationResult addScene(EditorCoordinator& coordinator) {
    const SceneId id = makeUniqueSceneId(coordinator.document());
    // Display name mirrors the id's ordinal: "scene-3" -> "Scene 3".
    const std::string name = "Scene " + id.substr(std::string("scene-").size());
    const EditorOperationResult result = coordinator.execute(CreateSceneCommand{id, name});
    // "Create Scene" means create-and-open: the command only ever touches
    // ProjectDocument (never the workspace), so without this the new scene sits
    // in the Hierarchy tab list while the viewport/Inspector keep showing
    // whatever was active before (or the empty state, if this was the first
    // scene) until the user clicks its tab.
    if (result.ok) coordinator.apply(SelectSceneIntent{id});
    return result;
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
    // The new type's display name mirrors its first instance ("Entity 3"),
    // not a shared "Entity" default: with every auto-created type named
    // identically, the Create menu's type catalog rendered as N
    // indistinguishable rows. Rename stays available (RenameObjectType), and
    // duplicate display names remain legal - the id disambiguates.
    return coordinator.execute(CreateEntityWithDefaultTypeCommand{
        sceneId, id, objectTypeId, /*objectTypeName*/ instanceName, instanceName,
        spawnPosition, /*layerId*/ coordinator.activeLayerId(sceneId)});
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

EditorOperationResult addTilemapEntity(EditorCoordinator& coordinator) {
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (sceneId.empty() || !scene) {
        return EditorOperationResult::failure("No active scene to add a Tilemap entity to");
    }
    // Preflight both preconditions here so failure produces a clear, targeted
    // message and no command runs (nothing is mutated or invalidated). Logged
    // explicitly: execute() logs its own rejections, but these short-circuit
    // before any command exists and must not be silent.
    const auto& tilesets = coordinator.document().data().tilesets;
    if (tilesets.empty()) {
        std::string message = "Import a tileset first - a Tilemap entity needs one to paint from";
        coordinator.logError(message);
        return EditorOperationResult::failure(std::move(message));
    }
    const std::string layerId = coordinator.activeLayerId(sceneId);
    const std::string targetLayer = layerId.empty() ? scene->defaultLayerId : layerId;
    if (coordinator.document().isLayerLocked(sceneId, targetLayer)) {
        std::string message = "Cannot create Tilemap entity: the active layer is locked";
        coordinator.logError(message);
        return EditorOperationResult::failure(std::move(message));
    }
    const EntityId id = nextAvailableEntityId(coordinator.document(), sceneId);
    // "Tilemap N" from 1, skipping taken names - matches the palette header's
    // display ("Painting: Tilemap 1 - ...") from the very first create.
    std::string instanceName;
    for (int n = 1;; ++n) {
        std::string candidate = "Tilemap " + std::to_string(n);
        bool taken = false;
        for (const SceneInstanceDef& inst : scene->instances) {
            if (inst.instanceName == candidate) { taken = true; break; }
        }
        if (!taken) { instanceName = std::move(candidate); break; }
    }
    const std::string objectTypeId = makeUniqueObjectTypeId(coordinator.document());
    // Origin, not the viewport centre: the tile grid must align with the
    // scene's world grid, and painting (not the transform) decides where the
    // visible content lives.
    const EditorOperationResult result = coordinator.execute(CreateTilemapEntityCommand{
        sceneId, id, objectTypeId, /*objectTypeName*/ instanceName, instanceName,
        Vec2{0.f, 0.f}, targetLayer, tilesets.front().assetId});
    if (result.ok) {
        // Workspace follow-ups (not undo-history entries): selecting the new
        // tilemap makes the Tile Palette dock visible, and Brush arms painting
        // immediately - create -> paint with zero extra clicks.
        coordinator.apply(SelectEntityIntent{id});
        coordinator.apply(SetActiveToolIntent{EditorTool::Brush});
    }
    return result;
}

EditorOperationResult addInstanceOfTypeAt(EditorCoordinator& coordinator,
                                          const std::string& objectTypeId,
                                          Vec2 spawnPosition) {
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (sceneId.empty() || !scene) {
        return EditorOperationResult::failure("No active scene to add an instance to");
    }
    // The chosen type must really exist in the catalog (no "Entity" fallback, no
    // first-available guess); otherwise fail without mutating anything.
    const EntityDef* type = coordinator.document().findObjectType(objectTypeId);
    if (!type) {
        return EditorOperationResult::failure("Unknown object type");
    }
    const EntityId id = nextAvailableEntityId(coordinator.document(), sceneId);
    const std::string name = makeUniqueInstanceName(*scene, type->name);
    // Reuse the existing structural command — a new instance bound to the existing
    // type (shared components, no ObjectTypeDef duplicated).
    const EditorOperationResult result =
        coordinator.execute(CreateEntityCommand{
            sceneId, id, objectTypeId, name, spawnPosition,
            /*layerId*/ coordinator.activeLayerId(sceneId)});
    // Select the new instance — workspace state, not an undo-history entry.
    if (result.ok) coordinator.apply(SelectEntityIntent{id});
    return result;
}

EditorOperationResult addInstanceOfType(EditorCoordinator& coordinator,
                                        const std::string& objectTypeId) {
    const SceneDef* scene = coordinator.document().findScene(coordinator.state().activeSceneId);
    if (!scene) return EditorOperationResult::failure("No active scene to add an instance to");
    return addInstanceOfTypeAt(
        coordinator, objectTypeId,
        normalizeSpawnPosition(
            Vec2{scene->worldSize.x * 0.5f, scene->worldSize.y * 0.5f},
            scene->worldSize));
}

EditorOperationResult addInstanceOfSelectedTypeAt(EditorCoordinator& coordinator,
                                                  Vec2 spawnPosition) {
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const EntityId selected = coordinator.selection().primaryEntity;
    const SceneInstanceDef* source =
        coordinator.document().findInstanceInScene(sceneId, selected);
    if (!source) {
        return EditorOperationResult::failure("Select an entity to instance its type");
    }
    return addInstanceOfTypeAt(coordinator, source->objectTypeId, spawnPosition);
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

EditorOperationResult cloneSelectedEntity(EditorCoordinator& coordinator) {
    return duplicateSelectedEntity(coordinator);
}

EditorOperationResult duplicateSelectedEntity(EditorCoordinator& coordinator) {
    const SceneId& sceneId = coordinator.state().activeSceneId;
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (!scene) return EditorOperationResult::failure("No active scene to duplicate into");
    const EntityId sourceId = coordinator.selection().primaryEntity;
    const SceneInstanceDef* source =
        coordinator.document().findInstanceInScene(sceneId, sourceId);
    if (!source) {
        return EditorOperationResult::failure("Select an entity to duplicate");
    }
    const EntityId newId = nextAvailableEntityId(coordinator.document(), sceneId);
    const std::string newName = makeUniqueInstanceName(*scene, source->instanceName);
    const Vec2 newPosition = normalizeSpawnPosition(
        Vec2{source->transform.position.x + kDuplicateOffset,
             source->transform.position.y + kDuplicateOffset},
        scene->worldSize);
    const EditorOperationResult result = coordinator.execute(
        DuplicateInstanceCommand{sceneId, sourceId, newId, newName, newPosition});
    if (result.ok) coordinator.apply(SelectEntityIntent{newId});
    return result;
}

} // namespace ArtCade::EditorNative
