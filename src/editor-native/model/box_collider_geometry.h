#pragma once

#include "core/types.h"

namespace ArtCade::EditorNative {

struct WorldRect {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
};

// Single editor/runtime BoxCollider2D world-bounds formula. The collider is
// centered at transform.position + offset*|scale|; size is the AABB extent
// multiplied by |Transform.scale| (x/y independently). Matches Play.
WorldRect boxColliderWorldBounds(const Transform& transform, Vec2 offset, Vec2 size);
WorldRect boxColliderWorldBounds(const Transform& transform,
                                 const BoxCollider2DComponent& collider);

} // namespace ArtCade::EditorNative
