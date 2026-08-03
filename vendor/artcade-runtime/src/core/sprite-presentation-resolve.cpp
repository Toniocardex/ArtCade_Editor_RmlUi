#include "sprite-presentation-resolve.h"

#include <cmath>

namespace ArtCade {

bool isValidNormalizedPivot(Vec2 pivot) {
    return std::isfinite(pivot.x) && std::isfinite(pivot.y)
        && pivot.x >= 0.f && pivot.x <= 1.f
        && pivot.y >= 0.f && pivot.y <= 1.f;
}

EffectiveSpritePresentation resolveEffectiveSpritePresentation(
    const EntityDef& objectType,
    const SpritePresentationOverride* instanceOverride) {
    EffectiveSpritePresentation out;
    if (!objectType.spritePresentation) {
        // Instance override without OT presentation is invalid for consumers;
        // still report absent so callers can reject authoring separately.
        (void)instanceOverride;
        return out;
    }

    const SpritePresentationComponent& base = *objectType.spritePresentation;
    out.present = true;
    out.visible = base.visible;
    out.source = base.source;
    out.pivot = base.pivot;

    if (instanceOverride) {
        if (instanceOverride->visible) out.visible = *instanceOverride->visible;
        if (instanceOverride->source) out.source = *instanceOverride->source;
        if (instanceOverride->pivot) out.pivot = *instanceOverride->pivot;
    }
    return out;
}

} // namespace ArtCade
