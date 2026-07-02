#include "editor-native/model/box_collider_geometry.h"

namespace ArtCade::EditorNative {

WorldRect boxColliderWorldBounds(const Transform& transform, Vec2 offset, Vec2 size) {
    const Vec2 center{
        transform.position.x + offset.x,
        transform.position.y + offset.y,
    };
    return WorldRect{
        center.x - size.x * 0.5f,
        center.y - size.y * 0.5f,
        size.x,
        size.y,
    };
}

WorldRect boxColliderWorldBounds(const Transform& transform,
                                 const BoxCollider2DComponent& collider) {
    return boxColliderWorldBounds(transform, collider.offset, collider.size);
}

} // namespace ArtCade::EditorNative
