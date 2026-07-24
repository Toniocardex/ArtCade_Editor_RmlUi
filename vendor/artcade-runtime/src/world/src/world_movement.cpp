#include "world_internal.h"
#include "world_platformer_controller.h"
#include "world_topdown_controller.h"

#include <algorithm>
#include <cmath>

namespace ArtCade {

namespace {
constexpr float kHorizontalMotionEpsilon = 0.01f;
constexpr float kVerticalMotionEpsilon   = 0.01f;
}

bool World::isPlatformerGrounded(EntityId id) const {
    PlatformerControllerComponent pc{};
    if (!entityGateway_.getPlatformerController(id, pc)) return false;
    const auto it = platformerRt_.find(id);
    const float vy = it != platformerRt_.end() ? it->second.velocity.y : 0.f;
    return collisionGrounded(id, vy);
}

bool World::isPlatformerFalling(EntityId id) const {
    return platformerState(id) == PlatformerState::Falling;
}

bool World::isPlatformerMovingHorizontally(EntityId id) const {
    return platformerState(id) == PlatformerState::Moving;
}

PlatformerState World::platformerState(EntityId id) const {
    PlatformerControllerComponent pc{};
    if (!entityGateway_.getPlatformerController(id, pc)) {
        return PlatformerState::Stopped;
    }
    const auto it = platformerRt_.find(id);
    if (it == platformerRt_.end()) return PlatformerState::Stopped;
    const PlatformerRt& rt = it->second;

    const bool grounded = collisionGrounded(id, rt.velocity.y);
    if (grounded || rt.climbing) {
        return std::abs(rt.velocity.x) > kHorizontalMotionEpsilon
            ? PlatformerState::Moving
            : PlatformerState::Stopped;
    }

    if (rt.velocity.y < -kVerticalMotionEpsilon) return PlatformerState::Jumping;
    if (rt.velocity.y > kVerticalMotionEpsilon) return PlatformerState::Falling;
    // Apex: keep prior airborne phase (never a false Stopped for one frame).
    return rt.lastAirState;
}

void World::tickPlatformerControllers(float dt) {
    entityGateway_.forEachActivePlatformer(
        [this, dt](EntityId id, const PlatformerControllerComponent& pc) {
            WorldInternal::stepPlatformerController(*this, id, pc, dt);
        });
}

void World::tickTopDownControllers(float dt) {
    entityGateway_.forEachActiveTopDown(
        [this, dt](EntityId id, const TopDownControllerComponent& tc) {
            WorldInternal::stepTopDownController(*this, id, tc, dt);
        });
}

void World::tickLinearMovers(float dt) {
    entityGateway_.forEachActiveLinearMover(
        [this, dt](EntityId id, const LinearMoverComponent& lm) {
            if (lm._paused) return;
            PlatformerControllerComponent platformer{};
            if (entityGateway_.getPlatformerController(id, platformer))
                return;
            TopDownControllerComponent topDown{};
            if (entityGateway_.getTopDownController(id, topDown))
                return;

            const Vec2 direction = WorldInternal::normalizeOrZero({
                lm.directionX, lm.directionY });
            const Vec2 velocity = {
                direction.x * std::max(0.f, lm.speed),
                direction.y * std::max(0.f, lm.speed),
            };

            WorldInternal::applySteeringVelocity(
                physics_, entityGateway_, id, velocity, dt);
        });
}

void World::setMovementIntent(EntityId id, float directionX, float directionY) {
    if (id == INVALID_ENTITY) return;
    auto& intent = controlIntents_[id];
    intent.movement = {
        std::clamp(directionX, -1.f, 1.f),
        std::clamp(directionY, -1.f, 1.f)
    };
    intent.hasMovement = true;
}

void World::clearMovementIntent(EntityId id) {
    if (id == INVALID_ENTITY) return;
    auto it = controlIntents_.find(id);
    if (it == controlIntents_.end()) return;
    it->second.hasMovement = false;
    it->second.movement = {};
}

void World::clearFrameMovementIntents() {
    for (auto& [id, intent] : controlIntents_) {
        (void)id;
        // Jump stays edged until the platformer step consumes it this frame.
        intent.hasMovement = false;
        intent.movement = {};
    }
}

void World::clearTopDownMovementContributions() {
    clearFrameMovementIntents();
}

void World::addTopDownMovementContribution(EntityId id, Vec2 direction) {
    if (id == INVALID_ENTITY || !std::isfinite(direction.x) || !std::isfinite(direction.y)) return;
    TopDownControllerComponent topDown{};
    if (!entityGateway_.getTopDownController(id, topDown)) return;
    auto& intent = controlIntents_[id];
    intent.movement.x = std::clamp(intent.movement.x + direction.x, -1.f, 1.f);
    intent.movement.y = std::clamp(intent.movement.y + direction.y, -1.f, 1.f);
    intent.hasMovement = intent.movement.x != 0.f || intent.movement.y != 0.f;
}

void World::requestJump(EntityId id) {
    if (id == INVALID_ENTITY) return;
    controlIntents_[id].jumpRequested = true;
}

void World::tickSimpleMovementIntents(float dt) {
    constexpr float kDefaultSpeed = 240.f;
    for (const auto& [id, intent] : controlIntents_) {
        if (!intent.hasMovement) continue;
        PlatformerControllerComponent platformer{};
        if (entityGateway_.getPlatformerController(id, platformer)) continue;
        TopDownControllerComponent topDown{};
        if (entityGateway_.getTopDownController(id, topDown)) continue;

        const float ax = std::clamp(intent.movement.x, -1.f, 1.f);
        const float ay = std::clamp(intent.movement.y, -1.f, 1.f);
        if (ax == 0.f && ay == 0.f) continue;

        Transform transform{};
        if (!entityGateway_.getTransform(id, transform)) continue;
        transform.position.x += ax * kDefaultSpeed * dt;
        transform.position.y += ay * kDefaultSpeed * dt;
        entityGateway_.setTransform(id, transform);
    }
}

} // namespace ArtCade
