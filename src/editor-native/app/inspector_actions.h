#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_operation_result.h"

namespace ArtCade::EditorNative {

class EditorCoordinator;

// =============================================================================
// Inspector actions — the component edits wired from the Inspector, kept UI-free
// so they are unit-testable without RmlUi (like hierarchy_actions). Each reads
// the authoritative selection and active scene, builds exactly one command for
// that explicit (scene, entity), and forwards it to the coordinator. No generic
// property bag, no component registry; one typed action per operation.
// =============================================================================

/** Add a sprite renderer to the selected Object Type. */
EditorOperationResult addSpriteRenderer(EditorCoordinator& coordinator);

/** Add a Sprite Animator to the selected Object Type using a valid project
 *  animation and its first clip as deterministic defaults. */
EditorOperationResult addSpriteAnimator(EditorCoordinator& coordinator);

/** Remove the sprite renderer from the selected Object Type. */
EditorOperationResult removeSpriteRenderer(EditorCoordinator& coordinator);

/** Set the selected sprite renderer's visibility. */
EditorOperationResult setSpriteRendererVisible(EditorCoordinator& coordinator, bool visible);

/** Set the selected sprite renderer's image asset ("" clears it). */
EditorOperationResult setSpriteRendererAsset(EditorCoordinator& coordinator, const AssetId& assetId);

/** Set the selected sprite renderer's animation asset. */
EditorOperationResult setSpriteRendererAnimation(EditorCoordinator& coordinator,
                                                 const AssetId& assetId);

/** Move the selected entity's editor bounds inside the active scene. */
EditorOperationResult bringSelectedEntityIntoScene(EditorCoordinator& coordinator);

EditorOperationResult addBoxCollider(EditorCoordinator& coordinator);
EditorOperationResult removeBoxCollider(EditorCoordinator& coordinator);
EditorOperationResult setBoxColliderOffset(EditorCoordinator& coordinator, Vec2 offset);
EditorOperationResult setBoxColliderSize(EditorCoordinator& coordinator, Vec2 size);
EditorOperationResult setBoxColliderEnabled(EditorCoordinator& coordinator, bool enabled);
EditorOperationResult setBoxColliderMode(EditorCoordinator& coordinator, BoxColliderMode mode);

EditorOperationResult addLinearMover(EditorCoordinator& coordinator);
EditorOperationResult removeLinearMover(EditorCoordinator& coordinator);
EditorOperationResult setLinearMoverDirection(EditorCoordinator& coordinator, Vec2 direction);
EditorOperationResult setLinearMoverSpeed(EditorCoordinator& coordinator, float speed);

EditorOperationResult addTopDownController(EditorCoordinator& coordinator);
EditorOperationResult removeTopDownController(EditorCoordinator& coordinator);
EditorOperationResult setTopDownControllerSpeed(EditorCoordinator& coordinator, float speed);
EditorOperationResult setTopDownControllerAcceleration(EditorCoordinator& coordinator, float value);
EditorOperationResult setTopDownControllerFriction(EditorCoordinator& coordinator, float value);
EditorOperationResult setTopDownControllerFourDirections(EditorCoordinator& coordinator, bool value);

EditorOperationResult addPlatformerController(EditorCoordinator& coordinator);
EditorOperationResult removePlatformerController(EditorCoordinator& coordinator);
EditorOperationResult setPlatformerMoveSpeed(EditorCoordinator& coordinator, float value);
EditorOperationResult setPlatformerJumpSpeed(EditorCoordinator& coordinator, float value);
EditorOperationResult setPlatformerGravity(EditorCoordinator& coordinator, float value);

/** Add a Tilemap component to the selected instance, auto-assigned to the
 *  project's first TilesetAsset (the Inspector's tileset picker changes it
 *  afterward). No selection or no tileset in the project -> no-op. */
EditorOperationResult addTilemapComponent(EditorCoordinator& coordinator);

/** Remove the Tilemap component from the selected instance. */
EditorOperationResult removeTilemapComponent(EditorCoordinator& coordinator);

} // namespace ArtCade::EditorNative
