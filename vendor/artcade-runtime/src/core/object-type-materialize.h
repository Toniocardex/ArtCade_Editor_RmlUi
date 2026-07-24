#pragma once

#include "types.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade {

/** ADR-0014: body type derived from gameplay drivers on the Object Type.
 *  Controllers / LinearMover → Kinematic; otherwise Static. */
BodyType resolveCollisionBodyType(const EntityDef& objectType);

/**
 * ADR-0014: pure BoxCollider2D → session CollisionBody.
 * Absent or disabled collider → nullopt (no runtime body).
 * Does not read EntityDef.collisionBody (authoring authority is BoxCollider2D).
 */
std::optional<CollisionBodyComponent> materializeBoxCollider2D(const EntityDef& objectType);

/** Merge type prototype + instance placement → runtime EntityDef.
 *  `animationAssets` supplies Animation-source sheet ids
 *  (`SpriteAnimationAssetDef::sourceImageAssetId`); see ADR-0010.
 *  Also derives CollisionBody from BoxCollider2D (ADR-0014). */
EntityDef materializeInstance(
    const EntityDef& typeProto,
    const SceneInstanceDef& instance,
    const std::vector<SpriteAnimationAssetDef>& animationAssets);

/** When objectTypes + scene.instances are present, rebuild entities + entityIds. */
void materializeProjectEntities(ProjectDoc& doc);

/** Populate spawn templates: objectTypes first, then first entity per class. */
void rebuildClassPrototypes(
    std::unordered_map<std::string, EntityDef>& out,
    const std::unordered_map<std::string, EntityDef>& objectTypes,
    const std::unordered_map<EntityId, EntityDef>& entityDefs);

/** Apply ImageAssetDef.defaultPivot to sprites with pivotFromAsset. */
void resolveSpritePivotsFromImageAssets(ProjectDoc& doc);

} // namespace ArtCade
