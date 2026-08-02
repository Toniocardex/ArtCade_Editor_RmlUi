#include "world_platformer_controller.h"
#include "../include/world.h"

#include "../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../modules/physics/include/physics.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::WorldInternal {

namespace {

constexpr float kHorizontalMotionEpsilon = 0.01f;
constexpr float kVerticalMotionEpsilon = 0.01f;

PlatformerState resolvePublishedState(
    bool grounded,
    bool climbing,
    bool wallSliding,
    const Vec2& velocity,
    PlatformerState lastAirState)
{
    if (climbing)
        return PlatformerState::Climbing;
    if (grounded) {
        return std::abs(velocity.x) > kHorizontalMotionEpsilon
            ? PlatformerState::Moving
            : PlatformerState::Stopped;
    }
    if (wallSliding)
        return PlatformerState::WallSliding;
    if (velocity.y < -kVerticalMotionEpsilon)
        return PlatformerState::Jumping;
    if (velocity.y > kVerticalMotionEpsilon)
        return PlatformerState::Falling;
    return lastAirState;
}

} // namespace

void stepPlatformerController(World& world,
                              EntityId id,
                              const PlatformerControllerComponent& pc,
                              float dt)
{
    auto& rt = world.platformerRt_[id];
    const bool wasGrounded = rt.grounded;
    const PlatformerContactProjection previous = world.platformerContacts_[id];

    world.rebuildCollisionWorld();

    auto intentIt = world.controlIntents_.find(id);
    World::ControlIntent* intent = intentIt != world.controlIntents_.end()
        ? &intentIt->second
        : nullptr;

    // Promote next-step wall intents queued by PostSimulation of the prior tick.
    std::optional<WallJumpIntent> wallJump;
    std::optional<WallSlideIntent> wallSlide;
    if (intent) {
        wallJump = intent->nextWallJump;
        wallSlide = intent->nextWallSlide;
        intent->nextWallJump.reset();
        intent->nextWallSlide.reset();
    }

    const bool jumpPending = intent && intent->jumpRequested;
    if (intent)
        intent->jumpRequested = false;
    const bool jumpEdge = jumpPending && !rt.jumpPendingPrev;
    rt.jumpPendingPrev = jumpPending;
    if (jumpEdge)
        rt.jumpBufferTimer = pc.jumpBuffer;
    else
        rt.jumpBufferTimer = std::max(0.f, rt.jumpBufferTimer - dt);

    float inputX = 0.f;
    float inputY = 0.f;
    if (intent && intent->hasMovement) {
        inputX = std::clamp(intent->movement.x, -1.f, 1.f);
        inputY = std::clamp(intent->movement.y, -1.f, 1.f);
    }

    bool wallJumpedThisStep = false;
    float vx = 0.f;
    float vy = rt.velocity.y;
    if (wallJump.has_value() && wallJump->pending
        && wallJump->side != PlatformerWallSide::None) {
        // Wall Jump > normal jump; capture side from intent (not re-queried).
        if (wallJump->side == PlatformerWallSide::Right)
            vx = -wallJump->horizontalSpeed;
        else
            vx = wallJump->horizontalSpeed;
        vy = -wallJump->verticalSpeed;
        rt.climbing = false;
        rt.coyoteTimer = 0.f;
        rt.jumpBufferTimer = 0.f;
        rt.grounded = false;
        wallJumpedThisStep = true;
    } else {
        vx = inputX * pc.maxSpeed;
    }

    constexpr bool ladderHorizontal = false;
    const float climbSpeed = pc.climbSpeed;
    CollisionWorld::Filter ladderFilter;
    ladderFilter.role = "interaction";
    ladderFilter.response = "sensor";
    const bool onLadder =
        world.firstCollisionTouching(id, ladderFilter) != INVALID_ENTITY;

    const float climbAxis = ladderHorizontal ? inputX : inputY;
    if (!onLadder)
        rt.climbing = false;
    else if (std::abs(climbAxis) > 0.f && !wallJumpedThisStep)
        rt.climbing = true;

    Transform transform{};
    if (!world.entityGateway_.getTransform(id, transform))
        return;

    rt.velocity.x = vx;
    rt.velocity.y = vy;

    // X phase
    const Transform beforeX = transform;
    transform.position.x += rt.velocity.x * dt;
    const PlatformerXMoveResult xMove =
        world.movePlatformerX(id, transform, beforeX, vx);
    if (xMove.blocked && !wallJumpedThisStep)
        vx = 0.f;
    // Wall jump impulse must not be zeroed by the wall we just left this step.
    if (wallJumpedThisStep)
        vx = rt.velocity.x;
    rt.velocity.x = vx;
    transform.velocity = rt.velocity;

    std::optional<GroundSupport> support;
    if (!wallJumpedThisStep) {
        support = world.findGroundSupport(
            id, transform, beforeX, rt.velocity.y, /*allowFloorSnap=*/false);
    }

    const bool canJump =
        support.has_value() || rt.coyoteTimer > 0.f || rt.climbing;
    bool jumpedThisStep = wallJumpedThisStep;
    if (!wallJumpedThisStep && rt.jumpBufferTimer > 0.f && canJump) {
        vy = -pc.jumpForce;
        rt.climbing = false;
        rt.coyoteTimer = 0.f;
        rt.jumpBufferTimer = 0.f;
        support.reset();
        jumpedThisStep = true;
    } else if (!wallJumpedThisStep && rt.climbing) {
        vy = ladderHorizontal ? 0.f : (climbAxis * climbSpeed);
    } else if (!wallJumpedThisStep && !support.has_value()) {
        vy += pc.customGravity * dt;
    } else if (!wallJumpedThisStep && vy > 0.f) {
        vy = 0.f;
    }

    // Wall slide: after gravity, before Y. Intent + current X block on the
    // requested side (not a persistent wall probe). Active even when vy is
    // already below maxFallSpeed so WallSliding can publish without a clamp.
    const bool blockedOnRequestedSide =
        wallSlide.has_value()
        && (
            (wallSlide->side == PlatformerWallSide::Left && xMove.hitLeftWall)
            || (wallSlide->side == PlatformerWallSide::Right
                && xMove.hitRightWall)
        );
    const bool validMaxFallSpeed =
        wallSlide.has_value()
        && std::isfinite(wallSlide->maxFallSpeed)
        && wallSlide->maxFallSpeed >= 0.f;
    const bool wallSlideActive =
        !wallJumpedThisStep
        && wallSlide.has_value()
        && wallSlide->pending
        && validMaxFallSpeed
        && blockedOnRequestedSide
        && !rt.climbing
        && !support.has_value()
        && vy >= 0.f;
    if (wallSlideActive)
        vy = std::min(vy, wallSlide->maxFallSpeed);

    rt.velocity.y = vy;
    transform.velocity = rt.velocity;

    const float preResolutionVy = vy;

    // Y phase — beforeY is post-X (never pre-X).
    const Transform beforeY = transform;
    transform.position.y += rt.velocity.y * dt;
    const PlatformerYMoveResult yMove =
        world.movePlatformerY(id, transform, beforeY, vy);

    if (yMove.hitCeiling && vy < 0.f)
        vy = 0.f;
    if (yMove.hitFloor && vy >= 0.f)
        vy = 0.f;

    const bool mayFloorSnap =
        wasGrounded
        && !jumpedThisStep
        && !rt.climbing
        && vy >= 0.f
        && !yMove.hitFloor;

    if (jumpedThisStep || rt.climbing) {
        if (vy < 0.f)
            support.reset();
        else
            support = world.findGroundSupport(
                id, transform, beforeY, vy, /*allowFloorSnap=*/false);
    } else if (vy >= 0.f) {
        if (yMove.hitFloor) {
            support = world.findGroundSupport(
                id, transform, beforeY, 0.f, /*allowFloorSnap=*/false);
        } else if (mayFloorSnap) {
            support = world.findGroundSupport(
                id, transform, beforeY, vy, /*allowFloorSnap=*/true);
            if (support.has_value()) {
                transform.position.y += support->correctionY;
                vy = 0.f;
                support = world.findGroundSupport(
                    id, transform, beforeY, 0.f, /*allowFloorSnap=*/false);
            }
        } else {
            support = world.findGroundSupport(
                id, transform, beforeY, vy, /*allowFloorSnap=*/false);
            if (support.has_value() && std::abs(support->correctionY) > 1e-6f
                && std::abs(support->correctionY) <= kGroundContactSkin) {
                transform.position.y += support->correctionY;
                vy = 0.f;
            }
        }
    } else {
        support.reset();
    }

    rt.velocity = { vx, vy };
    transform.velocity = rt.velocity;

    rt.grounded = support.has_value() && !rt.climbing;
    if (!rt.grounded && !rt.climbing) {
        if (rt.velocity.y < -kVerticalMotionEpsilon)
            rt.lastAirState = PlatformerState::Jumping;
        else if (rt.velocity.y > kVerticalMotionEpsilon)
            rt.lastAirState = PlatformerState::Falling;
    }
    const bool publishWallSliding =
        wallSlideActive && !rt.grounded && !rt.climbing;
    rt.state = resolvePublishedState(
        rt.grounded, rt.climbing, publishWallSliding, rt.velocity,
        rt.lastAirState);

    if (rt.grounded)
        rt.coyoteTimer = pc.coyoteTime;
    else
        rt.coyoteTimer = std::max(0.f, rt.coyoteTimer - dt);

    PlatformerStepContacts contacts;
    contacts.hitFloor = yMove.hitFloor;
    contacts.hitCeiling = yMove.hitCeiling;
    contacts.supportEntityId =
        support.has_value() ? support->supportEntityId : INVALID_ENTITY;
    world.platformerStepContacts_[id] = contacts;

    PlatformerContactProjection next{};
    next.grounded = rt.grounded;
    next.supportEntityId = contacts.supportEntityId;
    next.blockedLeftThisStep = xMove.hitLeftWall;
    next.blockedRightThisStep = xMove.hitRightWall;
    if (xMove.hitLeftWall)
        next.leftWallEntityId = xMove.contactEntityId;
    if (xMove.hitRightWall)
        next.rightWallEntityId = xMove.contactEntityId;
    next.hitCeilingThisStep = yMove.hitCeiling;
    if (yMove.hitCeiling)
        next.ceilingEntityId = yMove.contactEntityId;
    next.landedThisStep =
        !wasGrounded && rt.grounded && yMove.hitFloor;
    if (next.landedThisStep)
        next.landingImpactSpeed = std::max(0.f, preResolutionVy);
    next.blockedLeftEdgeThisStep =
        next.blockedLeftThisStep && !previous.blockedLeftThisStep;
    next.blockedRightEdgeThisStep =
        next.blockedRightThisStep && !previous.blockedRightThisStep;
    world.platformerContacts_[id] = next;

    world.entityGateway_.setTransform(id, transform);
    const uint32_t handle = world.entityGateway_.physicsHandle(id);
    if (handle != 0) {
        world.physics_.setPosition(handle, transform.position);
        world.physics_.setLinearVelocity(handle, transform.velocity);
    }
}

} // namespace ArtCade::WorldInternal
