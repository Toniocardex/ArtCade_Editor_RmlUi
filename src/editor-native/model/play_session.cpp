#include "editor-native/model/play_session.h"

#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

constexpr float kOneWayContactEpsilon = 0.001f;

const ImageAssetDef* findImageAsset(const ProjectDocument& document, const AssetId& id) {
    for (const ImageAssetDef& asset : document.data().imageAssets) {
        if (asset.assetId == id) return &asset;
    }
    return nullptr;
}

const SpriteAnimationAssetDef* findSpriteAnimationAsset(const ProjectDocument& document,
                                                        const AssetId& id) {
    for (const SpriteAnimationAssetDef& asset : document.data().spriteAnimationAssets) {
        if (asset.id == id) return &asset;
    }
    return nullptr;
}

const SpriteAnimationClipDef* findAnimationClip(const SpriteAnimationAssetDef& asset,
                                                const std::string& clipId) {
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (clip.id == clipId) return &clip;
    }
    return nullptr;
}

const RuntimeSpriteAnimationClip* findRuntimeAnimationClip(
    const RuntimeSpriteAnimationAsset& asset, const std::string& clipId) {
    for (const RuntimeSpriteAnimationClip& clip : asset.clips) {
        if (clip.id == clipId) return &clip;
    }
    return nullptr;
}

AnimationFrameRect frameRect(const SpriteAnimationFrameDef& frame) {
    return AnimationFrameRect{
        static_cast<float>(frame.x),
        static_cast<float>(frame.y),
        static_cast<float>(frame.width),
        static_cast<float>(frame.height),
    };
}

const Vec3* fillFor(const ProjectDocument& document, const std::string& typeId) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(typeId);
    return it == types.end() ? nullptr : &it->second.sprite.fillColor;
}

RuntimeSpriteAnimationAsset materializeSpriteAnimationAsset(
    const SpriteAnimationAssetDef& asset) {
    RuntimeSpriteAnimationAsset runtime;
    runtime.id = asset.id;
    runtime.imageAssetId = asset.imageId;
    runtime.clips.reserve(asset.clips.size());
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        runtime.clips.push_back(RuntimeSpriteAnimationClip{
            clip.id,
            clip.frames,
            clip.framesPerSecond,
            clip.playbackMode,
        });
    }
    return runtime;
}

// Mirrors the canonical runtime (World::tickLinearMovers): velocity is the
// normalized authored direction scaled by a non-negative speed.
Vec2 normalizeOrZero(Vec2 v) {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len <= 0.f) return Vec2{0.f, 0.f};
    return Vec2{v.x / len, v.y / len};
}

const LinearMoverComponent* moverFor(const ProjectDocument& document,
                                     const std::string& typeId) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(typeId);
    if (it == types.end() || !it->second.linearMover) return nullptr;
    return &*it->second.linearMover;
}

const TopDownControllerComponent* controllerFor(const ProjectDocument& document,
                                                const std::string& typeId) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(typeId);
    if (it == types.end() || !it->second.topDownController) return nullptr;
    return &*it->second.topDownController;
}

const PlatformerControllerComponent* platformerFor(const ProjectDocument& document,
                                                   const std::string& typeId) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(typeId);
    if (it == types.end() || !it->second.platformerController) return nullptr;
    return &*it->second.platformerController;
}

const BoxCollider2DComponent* colliderFor(const ProjectDocument& document,
                                          const std::string& typeId) {
    const auto& types = document.data().objectTypes;
    const auto it = types.find(typeId);
    if (it == types.end() || !it->second.boxCollider2D) return nullptr;
    return &*it->second.boxCollider2D;
}

// Movers are constrained only by a solid body collider. Trigger and one-way
// colliders can be authored on an object type, but they do not make that mover
// itself a blocking body.
bool isSolidMover(const std::optional<RuntimeBoxCollider>& collider) {
    return collider && collider->enabled && collider->mode == BoxColliderMode::Solid;
}

bool isStaticObstacle(const std::optional<RuntimeBoxCollider>& collider) {
    return collider && collider->enabled
        && (collider->mode == BoxColliderMode::Solid
            || collider->mode == BoxColliderMode::OneWayPlatform);
}

// Largest fraction of `move` allowed along one axis before the mover box [lo,hi]
// would penetrate a solid box [sLo,sHi], given the boxes already overlap on the
// cross axis. Only solids genuinely ahead constrain the move, so an already-
// penetrated box never yanks the mover back (no auto-depenetration).
float clampAxis(float move, float lo, float hi, float sLo, float sHi) {
    if (move > 0.f) {
        const float gap = sLo - hi;            // distance to the solid ahead
        if (gap >= 0.f && gap < move) return gap;
    } else if (move < 0.f) {
        const float gap = sHi - lo;            // negative distance to the solid behind
        if (gap <= 0.f && gap > move) return gap;
    }
    return move;
}

float clampOneWayPlatform(float move, float moverBottom, float platformTop) {
    if (move <= 0.f) return move;
    if (moverBottom > platformTop + kOneWayContactEpsilon) return move;
    const float gap = platformTop - moverBottom;
    return (gap >= 0.f && gap <= move + kOneWayContactEpsilon) ? gap : move;
}

} // namespace

Aabb runtimeColliderBounds(const RuntimeEntity& entity) {
    const RuntimeBoxCollider& c = *entity.collider;
    const WorldRect bounds = boxColliderWorldBounds(entity.transform, c.offset, c.size);
    return Aabb{bounds.x, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height};
}

std::optional<PlaySession> PlaySession::materialize(const ProjectDocument& document,
                                                    const SceneId& sceneId,
                                                    std::string* error) {
    const SceneDef* scene = document.findScene(sceneId);
    if (!scene) {
        if (error) *error = "Cannot start Play: scene does not exist";
        return std::nullopt;
    }

    PlaySession session;
    session.scene().sourceSceneId = scene->id;
    session.scene().name = scene->name;
    session.scene().worldSize = scene->worldSize;
    session.scene().backgroundColor = scene->backgroundColor;

    for (const SceneInstanceDef& instance : scene->instances) {
        RuntimeEntity entity;
        entity.id = instance.id;
        entity.name = instance.instanceName;
        entity.transform = instance.transform;
        if (const Vec3* fill = fillFor(document, instance.objectTypeId)) {
            entity.fillColor = *fill;
        }
        // Exactly one movement writer per entity. The Add commands reject a
        // second driver, but a hand-edited file could still carry several, so
        // materialize with a fixed priority: Platformer > TopDown > LinearMover.
        const LinearMoverComponent* mover = moverFor(document, instance.objectTypeId);
        const TopDownControllerComponent* controller =
            controllerFor(document, instance.objectTypeId);
        const PlatformerControllerComponent* platformer =
            platformerFor(document, instance.objectTypeId);
        if (platformer) {
            entity.platformerController = RuntimePlatformerController{
                std::max(0.f, platformer->maxSpeed),      // Move Speed
                std::max(0.f, platformer->jumpForce),     // Jump Speed
                std::max(0.f, platformer->customGravity), // Gravity
                0.f, false};
        } else if (controller) {
            entity.topDownController = RuntimeTopDownController{std::max(0.f, controller->maxSpeed)};
        } else if (mover && !mover->_paused) {
            const Vec2 dir = normalizeOrZero(Vec2{mover->directionX, mover->directionY});
            const float speed = std::max(0.f, mover->speed);
            entity.velocity = Vec2{dir.x * speed, dir.y * speed};
        }
        if (const BoxCollider2DComponent* box = colliderFor(document, instance.objectTypeId)) {
            entity.collider = RuntimeBoxCollider{box->offset, box->size, box->enabled, box->mode};
        }
        // A static solid is an obstacle: an active solid collider on an entity that
        // is not itself a kinematic mover (mover-vs-mover is out of scope).
        const bool isMover = (mover != nullptr) || (controller != nullptr) || (platformer != nullptr);
        if (!isMover && isStaticObstacle(entity.collider)) {
            session.staticColliders_.push_back(
                StaticRuntimeCollider{runtimeColliderBounds(entity), entity.collider->mode});
        }

        const SpriteRenderView sprite = resolveSpriteRenderer(document, sceneId, instance.id);
        if (sprite.present && !sprite.assetId.empty()) {
            const ImageAssetDef* image = findImageAsset(document, sprite.assetId);
            if (!image) {
                if (error) {
                    *error = "Cannot start Play: sprite references missing image asset "
                           + sprite.assetId;
                }
                return std::nullopt;
            }
            entity.sprite = RuntimeSpriteComponent{
                sprite.assetId, sprite.sourceRect, sprite.hasSourceRect, sprite.visible};
            session.assets_.imageAssets.emplace(
                image->assetId, RuntimeImageAsset{image->assetId, image->sourcePath});

            if (!sprite.animationAssetId.empty()) {
                const SpriteAnimationAssetDef* animation =
                    findSpriteAnimationAsset(document, sprite.animationAssetId);
                if (!animation || !instance.spriteAnimator) {
                    if (error) *error = "Cannot start Play: animation source is incomplete";
                    return std::nullopt;
                }
                const SpriteAnimationClipDef* clip =
                    findAnimationClip(*animation, instance.spriteAnimator->initialClipId);
                if (!clip) {
                    if (error) *error = "Cannot start Play: initial animation clip is missing";
                    return std::nullopt;
                }
                if (session.spriteAnimationAssets_.find(animation->id)
                    == session.spriteAnimationAssets_.end()) {
                    session.spriteAnimationAssets_.emplace(
                        animation->id, materializeSpriteAnimationAsset(*animation));
                }
                RuntimeSpriteAnimatorState animator;
                animator.animationAssetId = animation->id;
                animator.currentClipId = clip->id;
                animator.playbackSpeed = instance.spriteAnimator->playbackSpeed;
                animator.playing = instance.spriteAnimator->autoPlay && !clip->frames.empty();
                animator.finished = false;
                if (!clip->frames.empty()) {
                    entity.sprite->sourceRect = frameRect(clip->frames.front());
                    entity.sprite->hasSourceRect = true;
                }
                entity.spriteAnimator = std::move(animator);
            }
        }

        session.scene().entities.push_back(std::move(entity));
    }

    return session;
}

std::optional<PlaySession> PlaySession::startProject(const ProjectDocument& document,
                                                     std::string* error) {
    return materialize(document, document.startSceneId(), error);
}

std::optional<PlaySession> PlaySession::startActiveScene(const ProjectDocument& document,
                                                        const SceneId& sceneId,
                                                        std::string* error) {
    return materialize(document, sceneId, error);
}

const RuntimeEntity* PlaySession::findEntity(EntityId id) const {
    for (const RuntimeEntity& entity : scene_.entities) {
        if (entity.id == id) return &entity;
    }
    return nullptr;
}

KinematicMoveResult PlaySession::moveKinematicEntity(RuntimeEntity& entity, Vec2 desiredDelta) {
    KinematicMoveResult result;

    // A mover without an active solid collider is unconstrained.
    if (!isSolidMover(entity.collider)) {
        entity.transform.position.x += desiredDelta.x;
        entity.transform.position.y += desiredDelta.y;
        result.appliedDelta = desiredDelta;
        return result;
    }

    // -- X axis: clamp against every solid the mover overlaps on Y -------------
    {
        const Aabb m = runtimeColliderBounds(entity);
        float dx = desiredDelta.x;
        for (const StaticRuntimeCollider& s : staticColliders_) {
            if (s.mode != BoxColliderMode::Solid) continue;
            if (m.minY < s.bounds.maxY && s.bounds.minY < m.maxY) {
                dx = clampAxis(dx, m.minX, m.maxX, s.bounds.minX, s.bounds.maxX);
            }
        }
        if (desiredDelta.x > 0.f && dx < desiredDelta.x) result.hitRight = true;
        if (desiredDelta.x < 0.f && dx > desiredDelta.x) result.hitLeft = true;
        entity.transform.position.x += dx;
        result.appliedDelta.x = dx;
    }

    // -- Y axis: re-evaluate with the updated X so corners slide ---------------
    {
        const Aabb m = runtimeColliderBounds(entity);
        float dy = desiredDelta.y;
        for (const StaticRuntimeCollider& s : staticColliders_) {
            if (m.minX >= s.bounds.maxX || s.bounds.minX >= m.maxX) continue;
            if (s.mode == BoxColliderMode::Solid) {
                dy = clampAxis(dy, m.minY, m.maxY, s.bounds.minY, s.bounds.maxY);
            } else if (s.mode == BoxColliderMode::OneWayPlatform) {
                dy = clampOneWayPlatform(dy, m.maxY, s.bounds.minY);
            }
        }
        // World +Y is down: a clamped downward move is ground, upward is ceiling.
        if (desiredDelta.y > 0.f && dy < desiredDelta.y) result.hitGround = true;
        if (desiredDelta.y < 0.f && dy > desiredDelta.y) result.hitCeiling = true;
        entity.transform.position.y += dy;
        result.appliedDelta.y = dy;
    }

    return result;
}

void PlaySession::advance(float dt) {
    if (dt <= 0.f) return;
    for (RuntimeEntity& entity : scene_.entities) {
        if (!entity.sprite || !entity.spriteAnimator || !entity.spriteAnimator->playing) continue;
        RuntimeSpriteAnimatorState& animator = *entity.spriteAnimator;
        const auto assetIt = spriteAnimationAssets_.find(animator.animationAssetId);
        if (assetIt == spriteAnimationAssets_.end()) continue;
        const RuntimeSpriteAnimationClip* clip =
            findRuntimeAnimationClip(assetIt->second, animator.currentClipId);
        if (!clip || clip->frames.empty() || clip->framesPerSecond <= 0.f
            || animator.playbackSpeed <= 0.f) {
            continue;
        }
        animator.elapsedSeconds += dt * animator.playbackSpeed;
        const float frameDuration = 1.f / clip->framesPerSecond;
        while (animator.elapsedSeconds >= frameDuration && animator.playing) {
            animator.elapsedSeconds -= frameDuration;
            if (animator.currentFrameIndex + 1 < clip->frames.size()) {
                ++animator.currentFrameIndex;
            } else if (clip->playbackMode == AnimationPlaybackMode::Loop) {
                animator.currentFrameIndex = 0;
            } else {
                animator.finished = true;
                animator.playing = false;
            }
        }
        const std::size_t index =
            std::min(animator.currentFrameIndex, clip->frames.size() - 1);
        entity.sprite->sourceRect = frameRect(clip->frames[index]);
        entity.sprite->hasSourceRect = true;
    }
    // Authored velocity (LinearMover) routed through the one resolver.
    for (RuntimeEntity& entity : scene_.entities) {
        moveKinematicEntity(entity, Vec2{entity.velocity.x * dt, entity.velocity.y * dt});
    }
}

void PlaySession::updateTopDown(RuntimeEntity& entity, const RuntimeInputSnapshot& input,
                                float dt) {
    // Opposite inputs cancel; the diagonal is normalized so it is never faster.
    const Vec2 direction = normalizeOrZero(Vec2{
        static_cast<float>(input.moveRight) - static_cast<float>(input.moveLeft),
        static_cast<float>(input.moveDown) - static_cast<float>(input.moveUp),
    });
    if (direction.x == 0.f && direction.y == 0.f) return;
    const float speed = entity.topDownController->speed;
    moveKinematicEntity(entity, Vec2{direction.x * speed * dt, direction.y * speed * dt});
}

void PlaySession::updatePlatformer(RuntimeEntity& entity, const RuntimeInputSnapshot& input,
                                   float dt) {
    RuntimePlatformerController& pc = *entity.platformerController;

    // Jump is an edge input and only fires from the ground.
    if (input.jumpPressed && pc.grounded) {
        pc.verticalVelocity = -pc.jumpSpeed;   // -Y is up
        pc.grounded = false;
    }
    pc.verticalVelocity += pc.gravity * dt;    // +Y is down

    const float dx = (static_cast<float>(input.moveRight) - static_cast<float>(input.moveLeft))
                     * pc.moveSpeed * dt;
    const float dy = pc.verticalVelocity * dt;

    const KinematicMoveResult moved = moveKinematicEntity(entity, Vec2{dx, dy});

    if (moved.hitCeiling) pc.verticalVelocity = 0.f;   // stop rising into a ceiling
    if (moved.hitGround) {
        pc.grounded = true;
        pc.verticalVelocity = 0.f;
    } else {
        pc.grounded = false;                           // no floor contact this step
    }
}

void PlaySession::update(const RuntimeInputSnapshot& input, float dt) {
    if (!std::isfinite(dt) || dt <= 0.f) return;
    // One movement writer per entity (enforced at authoring): dispatch by driver.
    for (RuntimeEntity& entity : scene_.entities) {
        if (entity.topDownController)        updateTopDown(entity, input, dt);
        else if (entity.platformerController) updatePlatformer(entity, input, dt);
    }
}

} // namespace ArtCade::EditorNative
