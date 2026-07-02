#include "editor-native/app/inspector_actions.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/box_collider_commands.h"
#include "editor-native/commands/entity_commands.h"
#include "editor-native/commands/linear_mover_commands.h"
#include "editor-native/commands/platformer_controller_commands.h"
#include "editor-native/commands/sprite_commands.h"
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
} // namespace

EditorOperationResult addSpriteRenderer(EditorCoordinator& coordinator) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return EditorOperationResult::failure("No selected entity");
    }
    return coordinator.execute(AddSpriteRendererCommand{sceneId, id});
}

EditorOperationResult removeSpriteRenderer(EditorCoordinator& coordinator) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return EditorOperationResult::failure("No selected entity");
    }
    return coordinator.execute(RemoveSpriteRendererCommand{sceneId, id});
}

EditorOperationResult setSpriteRendererVisible(EditorCoordinator& coordinator, bool visible) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return EditorOperationResult::failure("No selected entity");
    }
    return coordinator.execute(SetSpriteRendererVisibleCommand{sceneId, id, visible});
}

EditorOperationResult setSpriteRendererAsset(EditorCoordinator& coordinator, const AssetId& assetId) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return EditorOperationResult::failure("No selected entity");
    }
    return coordinator.execute(SetSpriteRendererAssetCommand{sceneId, id, assetId});
}

EditorOperationResult setSpriteRendererAnimation(EditorCoordinator& coordinator,
                                                 const AssetId& assetId) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return EditorOperationResult::failure("No selected entity");
    }
    return coordinator.execute(SetSpriteRendererAnimationCommand{sceneId, id, assetId});
}

EditorOperationResult bringSelectedEntityIntoScene(EditorCoordinator& coordinator) {
    SceneId sceneId; EntityId id;
    if (!selectedTarget(coordinator, sceneId, id)) {
        return EditorOperationResult::failure("No selected entity");
    }
    const SceneInstanceDef* instance = coordinator.document().findInstanceInScene(sceneId, id);
    const SceneDef* scene = coordinator.document().findScene(sceneId);
    if (!instance || !scene) {
        return EditorOperationResult::failure("No selected entity");
    }

    const SceneFrameSnapshot frame =
        collectSceneFrameSnapshot(coordinator.document(), sceneId, id);
    const std::optional<WorldRect> bounds = editorBoundsForEntity(frame, id);
    if (!bounds) {
        return EditorOperationResult::failure("No editor bounds for selected entity");
    }
    if (classifySceneContainment(*bounds, frame.worldSize) == SceneContainment::Inside) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    const std::optional<Vec2> next =
        positionToBringBoundsInsideScene(*bounds, instance->transform.position, frame.worldSize);
    if (!next) {
        return EditorOperationResult::failure("Cannot bring selected entity into scene");
    }
    if (next->x == instance->transform.position.x && next->y == instance->transform.position.y) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    return coordinator.execute(SetEntityPositionCommand{sceneId, id, *next});
}

EditorOperationResult addBoxCollider(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(AddBoxColliderCommand{objectTypeId});
}

EditorOperationResult removeBoxCollider(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(RemoveBoxColliderCommand{objectTypeId});
}

EditorOperationResult setBoxColliderOffset(EditorCoordinator& coordinator, Vec2 offset) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(SetBoxColliderOffsetCommand{objectTypeId, offset});
}

EditorOperationResult setBoxColliderSize(EditorCoordinator& coordinator, Vec2 size) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(SetBoxColliderSizeCommand{objectTypeId, size});
}

EditorOperationResult setBoxColliderEnabled(EditorCoordinator& coordinator, bool enabled) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(SetBoxColliderEnabledCommand{objectTypeId, enabled});
}

EditorOperationResult setBoxColliderMode(EditorCoordinator& coordinator, BoxColliderMode mode) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(SetBoxColliderModeCommand{objectTypeId, mode});
}

EditorOperationResult addLinearMover(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(AddLinearMoverCommand{objectTypeId});
}

EditorOperationResult removeLinearMover(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(RemoveLinearMoverCommand{objectTypeId});
}

EditorOperationResult setLinearMoverDirection(EditorCoordinator& coordinator, Vec2 direction) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(SetLinearMoverDirectionCommand{objectTypeId, direction});
}

EditorOperationResult setLinearMoverSpeed(EditorCoordinator& coordinator, float speed) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(SetLinearMoverSpeedCommand{objectTypeId, speed});
}

EditorOperationResult addTopDownController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(AddTopDownControllerCommand{objectTypeId});
}

EditorOperationResult removeTopDownController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(RemoveTopDownControllerCommand{objectTypeId});
}

EditorOperationResult setTopDownControllerSpeed(EditorCoordinator& coordinator, float speed) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(SetTopDownControllerSpeedCommand{objectTypeId, speed});
}

EditorOperationResult addPlatformerController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(AddPlatformerControllerCommand{objectTypeId});
}

EditorOperationResult removePlatformerController(EditorCoordinator& coordinator) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(RemovePlatformerControllerCommand{objectTypeId});
}

EditorOperationResult setPlatformerMoveSpeed(EditorCoordinator& coordinator, float value) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(
        SetPlatformerValueCommand{objectTypeId, PlatformerField::MoveSpeed, value});
}

EditorOperationResult setPlatformerJumpSpeed(EditorCoordinator& coordinator, float value) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(
        SetPlatformerValueCommand{objectTypeId, PlatformerField::JumpSpeed, value});
}

EditorOperationResult setPlatformerGravity(EditorCoordinator& coordinator, float value) {
    std::string objectTypeId;
    if (!selectedObjectType(coordinator, objectTypeId)) {
        return EditorOperationResult::failure("No selected object type");
    }
    return coordinator.execute(
        SetPlatformerValueCommand{objectTypeId, PlatformerField::Gravity, value});
}

} // namespace ArtCade::EditorNative
