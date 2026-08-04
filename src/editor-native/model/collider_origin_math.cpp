#include "editor-native/model/collider_origin_math.h"

#include "editor-native/model/box_collider_geometry.h"

#include <cmath>

namespace ArtCade::EditorNative {

std::optional<ColliderOriginEdit> originEditForColliderAnchor(
    const Transform& transform,
    const BoxCollider2DComponent& effectiveCollider,
    ColliderAnchorX anchorX,
    ColliderAnchorY anchorY) {
    const float sx = std::abs(transform.scale.x);
    const float sy = std::abs(transform.scale.y);
    if (!std::isfinite(sx) || !std::isfinite(sy) || sx <= 0.f || sy <= 0.f) {
        return std::nullopt;
    }
    const WorldRect bounds = boxColliderWorldBounds(
        transform, effectiveCollider.offset, effectiveCollider.size);
    const float nx = anchorX == ColliderAnchorX::Left ? 0.f
        : anchorX == ColliderAnchorX::Center ? 0.5f : 1.f;
    const float ny = anchorY == ColliderAnchorY::Top ? 0.f
        : anchorY == ColliderAnchorY::Middle ? 0.5f : 1.f;
    const Vec2 nextPosition{
        bounds.x + bounds.width * nx,
        bounds.y + bounds.height * ny,
    };
    const Vec2 worldCenter{
        transform.position.x + effectiveCollider.offset.x * sx,
        transform.position.y + effectiveCollider.offset.y * sy,
    };
    const Vec2 nextOffset{
        (worldCenter.x - nextPosition.x) / sx,
        (worldCenter.y - nextPosition.y) / sy,
    };
    if (!std::isfinite(nextPosition.x) || !std::isfinite(nextPosition.y)
        || !std::isfinite(nextOffset.x) || !std::isfinite(nextOffset.y)) {
        return std::nullopt;
    }
    return ColliderOriginEdit{nextPosition, nextOffset};
}

} // namespace ArtCade::EditorNative
