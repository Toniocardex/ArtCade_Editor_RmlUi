#pragma once
// ADR-0057 — single effective Sprite Presentation (visible/source/pivot).
// UI-free, renderer-free, no asset or pivotFromAsset lookup.

#include "types.h"

namespace ArtCade {

struct EffectiveSpritePresentation {
    bool                     present = false;
    bool                     visible = true;
    SpritePresentationSource source = SpritePresentationNone{};
    Vec2                     pivot = {0.5f, 0.5f};
};

/** Primitive: Object Type + optional sparse instance override. */
EffectiveSpritePresentation resolveEffectiveSpritePresentation(
    const EntityDef& objectType,
    const SpritePresentationOverride* instanceOverride);

/** Object Type only (dynamic spawn / class prototypes). */
inline EffectiveSpritePresentation resolveEffectiveSpritePresentation(
    const EntityDef& objectType) {
    return resolveEffectiveSpritePresentation(objectType, nullptr);
}

/** Object Type + authored scene instance. */
inline EffectiveSpritePresentation resolveEffectiveSpritePresentation(
    const EntityDef& objectType,
    const SceneInstanceDef& instance) {
    const SpritePresentationOverride* overridePtr =
        instance.spritePresentationOverride
            ? &*instance.spritePresentationOverride
            : nullptr;
    return resolveEffectiveSpritePresentation(objectType, overridePtr);
}

/** Finite and closed-range [0,1] for both axes. */
bool isValidNormalizedPivot(Vec2 pivot);

} // namespace ArtCade
