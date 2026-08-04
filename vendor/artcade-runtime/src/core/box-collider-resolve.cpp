#include "box-collider-resolve.h"

namespace ArtCade {

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
