#include "world_internal.h"
#include "../../modules/renderer/include/renderer.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace ArtCade {

float clampCameraAxis(float worldExtent, float viewportExtent, float requestedCenter) {
    if (!std::isfinite(worldExtent) || worldExtent <= 0.f) return 0.f;
    if (!std::isfinite(viewportExtent) || viewportExtent <= 0.f) {
        return std::isfinite(requestedCenter) ? requestedCenter : worldExtent * 0.5f;
    }
    if (!std::isfinite(requestedCenter)) requestedCenter = worldExtent * 0.5f;
    if (viewportExtent >= worldExtent) return worldExtent * 0.5f;
    const float half = viewportExtent * 0.5f;
    return std::clamp(requestedCenter, half, worldExtent - half);
}

Vec2 clampCameraCenter(Vec2 worldSize, Vec2 viewportSize, Vec2 center) {
    return {
        clampCameraAxis(worldSize.x, viewportSize.x, center.x),
        clampCameraAxis(worldSize.y, viewportSize.y, center.y),
    };
}

std::optional<ResolvedCameraTarget> World::resolveCameraTarget() const {
    if (cameraFollowMode_ == CameraFollowMode::Disabled) return std::nullopt;

    if (cameraFollowMode_ == CameraFollowMode::Explicit) {
        Transform transform{};
        if (!entityGateway_.getTransform(cameraFollowTarget_, transform)) return std::nullopt;
        CameraTargetComponent config{};
        const bool hasConfig = entityGateway_.getCameraTarget(cameraFollowTarget_, config);
        ResolvedCameraTarget resolved;
        resolved.id = cameraFollowTarget_;
        resolved.desiredCenter = {
            transform.position.x + (hasConfig ? config.offsetX : 0.f),
            transform.position.y + (hasConfig ? config.offsetY : 0.f),
        };
        resolved.followSpeed = hasConfig ? config.followSpeed : 5.f;
        return resolved;
    }

    EntityId selected = INVALID_ENTITY;
    CameraTargetComponent config{};
    bool hasConfig = false;
    entityGateway_.forEachActiveCameraTarget(
        [&](EntityId id, const CameraTargetComponent& candidate) {
            if (selected == INVALID_ENTITY || id < selected) {
                selected = id;
                config = candidate;
                hasConfig = true;
            }
        });
    if (selected == INVALID_ENTITY || !hasConfig) return std::nullopt;
    Transform transform{};
    if (!entityGateway_.getTransform(selected, transform)) return std::nullopt;
    ResolvedCameraTarget resolved;
    resolved.id = selected;
    resolved.desiredCenter = {
        transform.position.x + config.offsetX,
        transform.position.y + config.offsetY,
    };
    resolved.followSpeed = config.followSpeed;
    return resolved;
}

void World::resetCameraForActiveScene() {
    const SceneDef* scene = entityGateway_.activeScene();
    // Automatic mode for scene entry so resolve picks the deterministic target.
    useAutomaticCameraTarget();
    Vec2 next{};
    if (const auto target = resolveCameraTarget()) {
        next = target->desiredCenter;
    } else if (scene) {
        next = {
            scene->cameraStart.x + scene->viewportSize.x * 0.5f,
            scene->cameraStart.y + scene->viewportSize.y * 0.5f,
        };
    }
    if (scene) {
        next = clampCameraCenter(scene->worldSize, scene->viewportSize, next);
    }
    cameraCenter_ = next;
    if (renderer_) renderer_->setCameraCenter(cameraCenter_);
}

void World::tickCameraTargets(float dt) {
    if (cameraFollowMode_ == CameraFollowMode::Disabled) return;

    if (cameraFollowMode_ == CameraFollowMode::Explicit) {
        Transform transform{};
        if (!entityGateway_.getTransform(cameraFollowTarget_, transform)) {
            useAutomaticCameraTarget();
        }
    }

    const auto resolved = resolveCameraTarget();
    if (!resolved) return;

    const Vec2 currentCenter = cameraCenter_;
    Vec2 nextCenter = resolved->desiredCenter;
    if (resolved->followSpeed > 0.f && dt > 0.f) {
        const float t = 1.f - std::exp(-resolved->followSpeed * dt);
        nextCenter = {
            currentCenter.x + (resolved->desiredCenter.x - currentCenter.x) * t,
            currentCenter.y + (resolved->desiredCenter.y - currentCenter.y) * t,
        };
    }

    if (const SceneDef* scene = entityGateway_.activeScene()) {
        nextCenter = clampCameraCenter(scene->worldSize, scene->viewportSize, nextCenter);
    }
    cameraCenter_ = nextCenter;
    if (renderer_) renderer_->setCameraCenter(cameraCenter_);
}

void World::tickHordeMembers(float dt) {
    struct HordeSnap {
        EntityId              id = 0;
        Vec2                  pos;
        HordeMemberComponent  cfg;
    };
    std::vector<HordeSnap> members;
    members.reserve(32);

    entityGateway_.forEachActiveHordeMember(
        [&](EntityId id, const HordeMemberComponent& horde) {
            // Skip entities that ALREADY have their own movement authority:
            // gameplay controllers (platformer / top-down / linear mover) own
            // velocity directly and we must not stomp them.
            PlatformerControllerComponent platformer{};
            if (entityGateway_.getPlatformerController(id, platformer))
                return;
            TopDownControllerComponent topDown{};
            if (entityGateway_.getTopDownController(id, topDown))
                return;
            LinearMoverComponent linear{};
            if (entityGateway_.getLinearMover(id, linear))
                return;

            Transform transform{};
            if (!entityGateway_.getTransform(id, transform)) return;
            members.push_back({ id, transform.position, horde });
        });

    for (const HordeSnap& self : members) {
        Vec2 steer{};

        float bestDist2 = 1e30f;
        Vec2 chaseDir{};
        for (EntityId targetId : entityGateway_.poolByClass(self.cfg.targetClass)) {
            if (targetId == self.id) continue;
            Transform targetTransform{};
            if (!entityGateway_.getTransform(targetId, targetTransform)) continue;
            const Vec2 toTarget = {
                targetTransform.position.x - self.pos.x,
                targetTransform.position.y - self.pos.y,
            };
            const float dist2 = WorldInternal::lengthSq(toTarget);
            if (dist2 < bestDist2) {
                bestDist2 = dist2;
                chaseDir = WorldInternal::normalizeOrZero(toTarget);
            }
        }
        steer.x += chaseDir.x * self.cfg.chaseWeight;
        steer.y += chaseDir.y * self.cfg.chaseWeight;

        const float sepR = self.cfg.separationRadius;
        if (sepR > 0.f) {
            for (const HordeSnap& other : members) {
                if (other.id == self.id) continue;
                Vec2 away = { self.pos.x - other.pos.x, self.pos.y - other.pos.y };
                float dist2 = WorldInternal::lengthSq(away);
                if (dist2 >= sepR * sepR) continue;
                float dist;
                if (dist2 < 1e-6f) {
                    away = (self.id < other.id) ? Vec2{ 1.f, 0.f } : Vec2{ -1.f, 0.f };
                    dist = 1.f;
                } else {
                    dist = std::sqrt(dist2);
                }
                const float strength = (sepR - dist) / sepR;
                steer.x += (away.x / dist) * strength * self.cfg.separationWeight;
                steer.y += (away.y / dist) * strength * self.cfg.separationWeight;
            }
        }

        const Vec2 dir = WorldInternal::normalizeOrZero(steer);
        const Vec2 velocity = {
            dir.x * std::max(0.f, self.cfg.maxSpeed),
            dir.y * std::max(0.f, self.cfg.maxSpeed),
        };
        WorldInternal::applySteeringVelocity(
            physics_, entityGateway_, self.id, velocity, dt);
    }
}

void World::tickMagneticItems(float dt) {
    entityGateway_.forEachActiveMagneticItem(
        [this, dt](EntityId magnetId, const MagneticItemComponent& mag) {
            if (!mag._enabled) return;
            Transform magnetTransform{};
            if (!entityGateway_.getTransform(magnetId, magnetTransform)) return;

            const Vec2 magnetPos = magnetTransform.position;
            const float maxDist = mag.radius;
            const float speed = std::max(0.f, mag.pullSpeed);

            for (EntityId itemId : entityGateway_.byTag(mag.attractTag)) {
                if (itemId == magnetId) continue;

                Transform itemTransform{};
                if (!entityGateway_.getTransform(itemId, itemTransform)) continue;

                const Vec2 toMagnet = {
                    magnetPos.x - itemTransform.position.x,
                    magnetPos.y - itemTransform.position.y,
                };
                const float dist2 = WorldInternal::lengthSq(toMagnet);
                if (maxDist > 0.f && dist2 > maxDist * maxDist) continue;

                const Vec2 dir = WorldInternal::normalizeOrZero(toMagnet);
                const Vec2 velocity = { dir.x * speed, dir.y * speed };

                WorldInternal::applySteeringVelocity(
                    physics_, entityGateway_, itemId, velocity, dt);
            }
        });
}

void World::tickAutoDestroy(float dt) {
    entityGateway_.forEachActiveAutoDestroy(
        [this, dt](EntityId id, AutoDestroyComponent& a) {
            if (a.lifespan <= 0.f) return;
            a._timeAlive += dt;
            if (a._timeAlive >= a.lifespan)
                entityGateway_.queueDestroy(id);
        });
}

} // namespace ArtCade
