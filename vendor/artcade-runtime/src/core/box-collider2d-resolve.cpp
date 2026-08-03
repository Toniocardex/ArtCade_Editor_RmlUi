#include "box-collider2d-resolve.h"

#include <cmath>

namespace ArtCade {

bool isValidBoxCollider2DOffset(Vec2 offset) {
    return std::isfinite(offset.x) && std::isfinite(offset.y);
}

EffectiveBoxCollider2D resolveEffectiveBoxCollider2D(
    const EntityDef& objectType,
    const BoxCollider2DOverride* instanceOverride) {
    EffectiveBoxCollider2D out;
    if (!objectType.boxCollider2D) return out;

    out.present = true;
    out.value = *objectType.boxCollider2D;
    if (instanceOverride && instanceOverride->offset) {
        out.value.offset = *instanceOverride->offset;
    }
    return out;
}

} // namespace ArtCade
