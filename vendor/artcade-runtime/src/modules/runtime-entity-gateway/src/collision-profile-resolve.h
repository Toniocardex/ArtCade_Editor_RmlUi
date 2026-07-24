#pragma once

#include "../../../core/types.h"

#include <string>
#include <unordered_map>

namespace ArtCade::Modules {
class SpriteAnimator;
}

namespace ArtCade::Modules::CollisionProfileResolve {

/**
 * Resolves authored collision shapes for an entity, merging inline body data
 * with optional sheet profiles (frame-normalized or world-space).
 */
bool resolve_collision_body(
    EntityId entityId,
    const SpriteComponent& sprite,
    const Transform& transform,
    const CollisionBodyComponent& authored,
    const std::unordered_map<std::string, CollisionProfileDef>& profiles,
    const std::unordered_map<std::string, std::string>& spritePathToAssetId,
    const SpriteAnimator* animator,
    CollisionBodyComponent& out);

} // namespace ArtCade::Modules::CollisionProfileResolve
