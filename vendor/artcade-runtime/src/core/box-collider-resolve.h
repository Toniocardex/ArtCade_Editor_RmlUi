#pragma once

// ADR-0058 — the only effective BoxCollider2D resolver used by editor and
// runtime materialisation.

#include "types.h"

namespace ArtCade {

struct EffectiveBoxCollider2D {
    bool present = false;
    BoxCollider2DComponent value{};
};

EffectiveBoxCollider2D resolveEffectiveBoxCollider2D(
    const EntityDef& objectType,
    const BoxCollider2DOverride* instanceOverride);

inline EffectiveBoxCollider2D resolveEffectiveBoxCollider2D(
    const EntityDef& objectType) {
    return resolveEffectiveBoxCollider2D(objectType, nullptr);
}

inline EffectiveBoxCollider2D resolveEffectiveBoxCollider2D(
    const EntityDef& objectType,
    const SceneInstanceDef& instance) {
    return resolveEffectiveBoxCollider2D(
        objectType,
        instance.boxCollider2DOverride ? &*instance.boxCollider2DOverride : nullptr);
}

} // namespace ArtCade
