#include "world_platformer_controller.h"
#include "../include/world.h"

#include "../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../modules/physics/include/physics.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::WorldInternal {

namespace {

constexpr int kStableGroundedFrames = 2;

} // namespace

void stepPlatformerController(World& world,
                              EntityId id,
                              const PlatformerControllerComponent& pc,
                              float dt)
{
    auto& rt = world.platformerRt_[id];

    float vy = rt.velocity.y;

    world.rebuildCollisionWorld();
    // ADR-0022: velocity-aware grounded (One Way ascending is not grounded).
    const bool rawGrounded = world.collisionGrounded(id, rt.velocity.y);

    if (rawGrounded) {
        rt.groundedFrames = std::min(rt.groundedFrames + 1, kStableGroundedFrames + 4);
        rt.airborneFrames = 0;
    } else {
        rt.airborneFrames = std::min(rt.airborneFrames + 1, 10000);
        rt.groundedFrames = 0;
    }
    const bool stableGrounded = rt.groundedFrames >= kStableGroundedFrames;

    if (stableGrounded)
        rt.coyoteTimer = pc.coyoteTime;
    else if (rt.airborneFrames > 0)
        rt.coyoteTimer = std::max(0.f, rt.coyoteTimer - dt);

    auto intentIt = world.controlIntents_.find(id);
    World::ControlIntent* intent = intentIt != world.controlIntents_.end()
        ? &intentIt->second
        : nullptr;

    const bool jumpPending = intent && intent->jumpRequested;
    if (intent)
        intent->jumpRequested = false;
    const bool jumpEdge = jumpPending && !rt.jumpPendingPrev;
    rt.jumpPendingPrev = jumpPending;
    if (jumpEdge)
        rt.jumpBufferTimer = pc.jumpBuffer;
    else
        rt.jumpBufferTimer = std::max(0.f, rt.jumpBufferTimer - dt);

    float vx = 0.f;
    float inputX = 0.f;
    float inputY = 0.f;

    if (intent && intent->hasMovement) {
        inputX = std::clamp(intent->movement.x, -1.f, 1.f);
        inputY = std::clamp(intent->movement.y, -1.f, 1.f);
        vx = inputX * pc.maxSpeed;
    }

    // Climb zones are interaction sensor shapes in CollisionWorld.
    bool  ladderHorizontal = false;
    float climbSpeed       = pc.climbSpeed;
    CollisionWorld::Filter ladderFilter;
    ladderFilter.role = "interaction";
    ladderFilter.response = "sensor";
    const bool onLadder =
        world.firstCollisionTouching(id, ladderFilter) != INVALID_ENTITY;

    // Engage on input along the ladder's axis (vertical by default); this keeps
    // a body walking past a vertical ladder from auto-grabbing it.
    const float climbAxis = ladderHorizontal ? inputX : inputY;
    if (!onLadder)
        rt.climbing = false;
    else if (std::abs(climbAxis) > 0.f)
        rt.climbing = true;

    const bool canJump = rawGrounded || rt.coyoteTimer > 0.f;
    if (rt.jumpBufferTimer > 0.f && (canJump || rt.climbing)) {
        vy = -pc.jumpForce;
        rt.climbing        = false;   // jumping detaches from the ladder
        rt.coyoteTimer     = 0.f;
        rt.jumpBufferTimer = 0.f;
        rt.airborneFrames  = 1;
        rt.groundedFrames  = 0;
    } else if (rt.climbing) {
        // Vertical ladder: drive vy by input. Horizontal rope: suspend gravity
        // and let the normal vx (above) carry traversal.
        vy = ladderHorizontal ? 0.f : (climbAxis * climbSpeed);
    } else if (!stableGrounded) {
        vy += pc.customGravity * dt;
    } else if (vy > 0.f) {
        vy = 0.f;
    }

    rt.velocity = { vx, vy };
    Transform transform{};
    if (!world.entityGateway_.getTransform(id, transform)) return;
    const Transform beforeMove = transform;
    transform.velocity = rt.velocity;
    transform.position.x += rt.velocity.x * dt;
    transform.position.y += rt.velocity.y * dt;

    const KinematicCollisionResult collision =
        world.resolveKinematicCollisionBody(id, transform, beforeMove, vx, vy);

    if (collision.grounded && !rt.climbing) {
        vy = 0.f;
    }

    rt.velocity = { vx, vy };
    transform.velocity = rt.velocity;

    world.entityGateway_.setTransform(id, transform);

    const uint32_t handle = world.entityGateway_.physicsHandle(id);
    if (handle != 0) {
        world.physics_.setPosition(handle, transform.position);
        world.physics_.setLinearVelocity(handle, transform.velocity);
    }

    // ADR-0016: latch definite airborne Jumping/Falling for apex hysteresis.
    constexpr float kVerticalMotionEpsilon = 0.01f;
    if (!world.collisionGrounded(id, rt.velocity.y) && !rt.climbing) {
        if (rt.velocity.y < -kVerticalMotionEpsilon) {
            rt.lastAirState = PlatformerState::Jumping;
        } else if (rt.velocity.y > kVerticalMotionEpsilon) {
            rt.lastAirState = PlatformerState::Falling;
        }
    }
}

} // namespace ArtCade::WorldInternal
