#include "editor-native/app/inspector_actions.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/box_collider_commands.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/linear_mover_commands.h"
#include "editor-native/commands/platformer_controller_commands.h"
#include "editor-native/commands/sprite_presentation_commands.h"
#include "editor-native/commands/tilemap_commands.h"
#include "editor-native/commands/top_down_controller_commands.h"
#include "editor-native/model/scene_frame_snapshot.h"

namespace ArtCade::EditorNative {

namespace {
// Resolve the authoritative (active scene, selected entity) target, or report a
// readable failure when there is nothing to edit. Returns false on no target.
bool selectedTarget(const EditorCoordinator& coordinator, SceneId& sceneId, EntityId& id) {
    sceneId = coordinator.state().activeSceneId;
    id      = coordinator.selection().primaryEntity;
    return !sceneId.empty() && id != INVALID_ENTITY;
}

bool selectedObjectType(const EditorCoordinator& coordinator, std::string& objectTypeId) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) return false;
    const SceneInstanceDef* instance = coordinator.document().findInstanceInScene(sceneId, id);
    if (!instance || instance->objectTypeId.empty()) return false;
    objectTypeId = instance->objectTypeId;
    return true;
}

// Every helper below is a thin select-target-then-execute wrapper: when
// execute() runs, a rejection is already logged automatically, but a missing
// target short-circuits before that and would otherwise return a failure
// nobody ever sees. Routed through here once instead of repeating the same
// log call at each of this file's ~25 call sites (contract: "ogni errore
// deve essere... non silenzioso").
EditorOperationResult fail(EditorCoordinator& coordinator, std::string message) {
    coordinator.logError(message);
    return EditorOperationResult::failure(std::move(message));
}
} // namespace

EditorOperationResult addSpriteRenderer(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected entity");
    }
    return coordinator.execute(AddSpriteRendererToObjectTypeCommand{objectTypeId});
}

EditorOperationResult removeSpriteRenderer(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected entity");
    }
    return coordinator.execute(RemoveSpriteRendererFromObjectTypeCommand{objectTypeId});
}

EditorOperationResult setSpriteRendererVisible(EditorCoordinator& coordinator, bool visible) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return fail(coordinator, "No selected entity");
    }
    const SceneInstanceDef* instance =
        coordinator.document().findInstanceInScene(sceneId, id);
    const EntityDef* type = instance
        ? coordinator.document().findObjectType(instance->objectTypeId) : nullptr;
    if (!instance || !type || !type->spriteRenderer) {
        return fail(coordinator, "Object Type has no SpriteRenderer");
    }
    SpriteRendererOverride delta = instance->spriteRendererOverride.value_or(
        SpriteRendererOverride{});
    delta.capabilityEnabled.reset();
    if (visible == type->spriteRenderer->visible) delta.visible.reset();
    else delta.visible = visible;
    if (!delta.imageAssetId && !delta.visible) {
        return coordinator.execute(ClearInstanceSpriteOverrideCommand{sceneId, id});
    }
    return coordinator.execute(SetInstanceSpriteOverrideCommand{sceneId, id, std::move(delta)});
}

EditorOperationResult setSpriteRendererAsset(EditorCoordinator& coordinator, const AssetId& assetId) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected entity");
    }
    const ObjectTypeSpriteSourceKind kind = assetId.empty()
        ? ObjectTypeSpriteSourceKind::None : ObjectTypeSpriteSourceKind::Image;
    return coordinator.execute(SetObjectTypeSpriteSourceCommand{objectTypeId, kind, assetId});
}

EditorOperationResult setSpriteRendererAnimation(EditorCoordinator& coordinator,
                                                 const AssetId& assetId) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected entity");
    }
    const ObjectTypeSpriteSourceKind kind = assetId.empty()
        ? ObjectTypeSpriteSourceKind::None : ObjectTypeSpriteSourceKind::Animation;
    return coordinator.execute(SetObjectTypeSpriteSourceCommand{objectTypeId, kind, assetId});
}

EditorOperationResult bringSelectedEntityIntoScene(EditorCoordinator& coordinator) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return fail(coordinator, "No selected entity");
    }
    const SceneInstanceDef* instance = coordinator.document().findInstanceInScene(sceneId, id);
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (!instance || !scene) {
        return fail(coordinator, "No selected entity");
    }

    const SceneFrameSnapshot frame =
        collectSceneFrameSnapshot(coordinator.document(), sceneId, id);
    const std::optional<WorldRect> bounds = editorBoundsForEntity(frame, id);
    if (!bounds) {
        return fail(coordinator, "No editor bounds for selected entity");
    }
    if (classifySceneContainment(*bounds, frame.worldSize) == SceneContainment::Inside) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    const std::optional<Vec2> next =
        positionToBringBoundsInsideScene(*bounds, instance->transform.position, frame.worldSize);
    if (!next) {
        return fail(coordinator, "Cannot bring selected entity into scene");
    }
    if (next->x == instance->transform.position.x && next->y == instance->transform.position.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    return coordinator.execute(SetEntityTransformCommand{
        sceneId, id, AuthoredTransformPatch{*next}});
}

EditorOperationResult addBoxCollider(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(AddBoxColliderCommand{objectTypeId});
}

EditorOperationResult removeBoxCollider(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(RemoveBoxColliderCommand{objectTypeId});
}

EditorOperationResult setBoxColliderOffset(EditorCoordinator& coordinator, Vec2 offset) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(SetBoxColliderOffsetCommand{objectTypeId, offset});
}

EditorOperationResult setBoxColliderSize(EditorCoordinator& coordinator, Vec2 size) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(SetBoxColliderSizeCommand{objectTypeId, size});
}

EditorOperationResult setBoxColliderEnabled(EditorCoordinator& coordinator, bool enabled) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(SetBoxColliderEnabledCommand{objectTypeId, enabled});
}

EditorOperationResult setBoxColliderMode(EditorCoordinator& coordinator, BoxColliderMode mode) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(SetBoxColliderModeCommand{objectTypeId, mode});
}

EditorOperationResult addLinearMover(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(AddLinearMoverCommand{objectTypeId});
}

EditorOperationResult removeLinearMover(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(RemoveLinearMoverCommand{objectTypeId});
}

EditorOperationResult setLinearMoverDirection(EditorCoordinator& coordinator, Vec2 direction) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(SetLinearMoverDirectionCommand{objectTypeId, direction});
}

EditorOperationResult setLinearMoverSpeed(EditorCoordinator& coordinator, float speed) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(SetLinearMoverSpeedCommand{objectTypeId, speed});
}

EditorOperationResult addTopDownController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(AddTopDownControllerCommand{objectTypeId});
}

EditorOperationResult removeTopDownController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(RemoveTopDownControllerCommand{objectTypeId});
}

EditorOperationResult setTopDownControllerSpeed(EditorCoordinator& coordinator, float speed) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(SetTopDownControllerSpeedCommand{objectTypeId, speed});
}

EditorOperationResult addPlatformerController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(AddPlatformerControllerCommand{objectTypeId});
}

EditorOperationResult removePlatformerController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(RemovePlatformerControllerCommand{objectTypeId});
}

EditorOperationResult setPlatformerMoveSpeed(EditorCoordinator& coordinator, float value) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(
        SetPlatformerValueCommand{objectTypeId, PlatformerField::MoveSpeed, value});
}

EditorOperationResult setPlatformerJumpSpeed(EditorCoordinator& coordinator, float value) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(
        SetPlatformerValueCommand{objectTypeId, PlatformerField::JumpSpeed, value});
}

EditorOperationResult setPlatformerGravity(EditorCoordinator& coordinator, float value) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return fail(coordinator, "No selected object type");
    }
    return coordinator.execute(
        SetPlatformerValueCommand{objectTypeId, PlatformerField::Gravity, value});
}

EditorOperationResult addTilemapComponent(EditorCoordinator& coordinator) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return fail(coordinator, "No selected entity");
    }
    const auto& tilesets = coordinator.document().data().tilesets;
    if (tilesets.empty()) {
        return fail(coordinator, "No tileset in the project - create one first");
    }
    TilemapComponent component;
    component.tilesetAssetId = tilesets.front().assetId;
    return coordinator.execute(AddTilemapComponentCommand{sceneId, id, component});
}

EditorOperationResult removeTilemapComponent(EditorCoordinator& coordinator) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return fail(coordinator, "No selected entity");
    }
    return coordinator.execute(RemoveTilemapComponentCommand{sceneId, id});
}

} // namespace ArtCade::EditorNative
