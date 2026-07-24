#pragma once

#include "../../../core/types.h"

namespace ArtCade::Modules {

struct EntityPhysicsFlags {
    bool hasExplicitCollider = false;
    bool hasPlatformer       = false;
    bool hasTopDown          = false;
};

struct PhysicsBodyRules {
    BodyType bodyType               = BodyType::Dynamic;
    float    gravityScale           = 1.f;
};

PhysicsBodyRules resolvePhysicsBodyRules(const PhysicsComponent& compIn,
                                         const EntityPhysicsFlags& flags);

void applyPhysicsBodyRules(PhysicsComponent& comp,
                           const PhysicsBodyRules& rules);

} // namespace ArtCade::Modules
