#include "world_topdown_controller.h"
#include "world_internal.h"
#include "../include/world.h"

#include "../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../modules/physics/include/physics.h"

namespace ArtCade::WorldInternal {

void stepTopDownController(World& world,
                           EntityId id,
                           const TopDownControllerComponent& tc,
                           float dt)
{
    PlatformerControllerComponent platformer{};
    if (world.entityGateway_.getPlatformerController(id, platformer))
        return;

    auto& rt = world.topDownRt_[id];
    auto intentIt = world.controlIntents_.find(id);
    World::ControlIntent* intent = intentIt != world.controlIntents_.end()
        ? &intentIt->second
        : nullptr;

    Vec2 targetVelocity{};
    if (intent && intent->hasMovement) {
        const Vec2 direction = constrainTopDownDirection(
            intent->movement, tc.fourDirections);
        targetVelocity = {
            direction.x * tc.maxSpeed,
            direction.y * tc.maxSpeed,
        };
        rt.velocity = approach(
            rt.velocity, targetVelocity, std::max(0.f, tc.acceleration) * dt);
    } else {
        rt.velocity = approach(
            rt.velocity, {}, std::max(0.f, tc.friction) * dt);
    }

    // ADR-0021: Top Down owns Transform and resolves against CollisionWorld
    // (BoxCollider2D authority), same path as Platformer. Do not return early
    // on a Physics handle — Physics is not the solid-blocking authority.
    Transform transform{};
    if (!world.entityGateway_.getTransform(id, transform)) return;
    const Transform beforeMove = transform;
    transform.velocity = rt.velocity;
    transform.position.x += rt.velocity.x * dt;
    transform.position.y += rt.velocity.y * dt;

    float vx = rt.velocity.x;
    float vy = rt.velocity.y;
    world.rebuildCollisionWorld();
    world.resolveKinematicCollisionBody(id, transform, beforeMove, vx, vy);
    rt.velocity = { vx, vy };
    transform.velocity = rt.velocity;
    world.entityGateway_.setTransform(id, transform);

    // Optional explicit Physics collider: keep the handle aligned. Top Down
    // never pulls from Physics (syncPhysicsToEntities skips it).
    const uint32_t handle = world.entityGateway_.physicsHandle(id);
    if (handle != 0) {
        world.physics_.setPosition(handle, transform.position);
        world.physics_.setLinearVelocity(handle, transform.velocity);
    }
}

} // namespace ArtCade::WorldInternal
