#include "../include/physics-body-rules.h"

namespace ArtCade::Modules {

PhysicsBodyRules resolvePhysicsBodyRules(const PhysicsComponent& compIn,
                                         const EntityPhysicsFlags& flags)
{
    PhysicsBodyRules rules{};
    rules.bodyType = compIn.bodyType;
    // Controllers that own Transform (ADR-0021 / Platformer) must not fall.
    rules.gravityScale =
        (flags.hasTopDown || flags.hasPlatformer) ? 0.f : 1.f;

    if (!flags.hasExplicitCollider)
        rules.bodyType = BodyType::Dynamic;
    // Transform-owned controllers: kinematic when a Physics body exists at all
    // (explicit collider only — see ensurePhysicsBody).
    if (flags.hasExplicitCollider
        && (flags.hasPlatformer || flags.hasTopDown))
        rules.bodyType = BodyType::Kinematic;

    return rules;
}

void applyPhysicsBodyRules(PhysicsComponent& comp,
                           const PhysicsBodyRules& rules)
{
    comp.bodyType = rules.bodyType;
}

} // namespace ArtCade::Modules
