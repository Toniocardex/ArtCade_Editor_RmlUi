#include "editor-native/model/box_collider_geometry.h"

#include <cmath>

namespace ArtCade::EditorNative {

WorldRect boxColliderWorldBounds(const Transform& transform, Vec2 offset, Vec2 size) {
    // Same contract as CollisionWorld::shapeInstance: local offset/size follow
    // Transform.scale magnitude so Edit overlay matches Play collision.
    const float sx = std::fabs(transform.scale.x);
    const float sy = std::fabs(transform.scale.y);
    const Vec2 scaledOffset{ offset.x * sx, offset.y * sy };
    const Vec2 scaledSize{ size.x * sx, size.y * sy };
    const Vec2 center{
        transform.position.x + scaledOffset.x,
        transform.position.y + scaledOffset.y,
    };
    return WorldRect{
        center.x - scaledSize.x * 0.5f,
        center.y - scaledSize.y * 0.5f,
        scaledSize.x,
        scaledSize.y,
    };
}

WorldRect boxColliderWorldBounds(const Transform& transform,
                                 const BoxCollider2DComponent& collider) {
    return boxColliderWorldBounds(transform, collider.offset, collider.size);
}

} // namespace ArtCade::EditorNative
