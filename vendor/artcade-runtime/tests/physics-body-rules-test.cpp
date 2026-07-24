// physics-body-rules-test.cpp — stateless policy for physics body setup.

#include "modules/runtime-entity-gateway/include/physics-body-rules.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using ArtCade::BodyType;
using ArtCade::Modules::EntityPhysicsFlags;
using ArtCade::Modules::PhysicsBodyRules;
using ArtCade::Modules::applyPhysicsBodyRules;
using ArtCade::Modules::resolvePhysicsBodyRules;
using ArtCade::PhysicsComponent;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

int main() {
    PhysicsComponent comp{};

    {
        EntityPhysicsFlags flags{};
        flags.hasExplicitCollider = true;
        flags.hasPlatformer = true;
        const PhysicsBodyRules rules = resolvePhysicsBodyRules(comp, flags);
        CHECK(rules.bodyType == BodyType::Kinematic);
        CHECK(std::abs(rules.gravityScale) < 0.001f);
    }

    {
        // ADR-0021 Slice 2: Top Down + explicit Physics collider → Kinematic
        // (same as Platformer; Transform owns motion).
        EntityPhysicsFlags flags{};
        flags.hasExplicitCollider = true;
        flags.hasTopDown = true;
        const PhysicsBodyRules rules = resolvePhysicsBodyRules(comp, flags);
        CHECK(rules.bodyType == BodyType::Kinematic);
        CHECK(std::abs(rules.gravityScale) < 0.001f);
    }

    {
        EntityPhysicsFlags flags{};
        flags.hasTopDown = true;
        // No explicit collider → ensurePhysicsBody will not create a body;
        // rules still zero gravity if a body were present for other reasons.
        const PhysicsBodyRules rules = resolvePhysicsBodyRules(comp, flags);
        CHECK(rules.bodyType == BodyType::Dynamic);
        CHECK(std::abs(rules.gravityScale) < 0.001f);
    }

    {
        EntityPhysicsFlags flags{};
        const PhysicsBodyRules rules = resolvePhysicsBodyRules(comp, flags);
        PhysicsComponent out{};
        applyPhysicsBodyRules(out, rules);
        CHECK(out.bodyType == BodyType::Dynamic);
    }

    if (g_failed == 0) {
        std::cout << "physics-body-rules-test: " << g_passed << " passed\n";
        return 0;
    }
    std::cerr << "physics-body-rules-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return 1;
}
