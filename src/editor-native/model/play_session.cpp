#include "editor-native/model/play_session.h"

#include "editor-native/model/box_collider_geometry.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"
#include "editor-native/model/tilemap_render_view.h"
#include "logic-core.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

struct PlaySession::LogicHostAdapter final : Logic::ILogicRuntimeHost {
    explicit LogicHostAdapter(PlaySession* owner) : owner(owner) {}

    bool setVisible(EntityId id, bool value) override {
        RuntimeEntity* entity = owner ? owner->findEntityMutable(id) : nullptr;
        if (!entity) return false;
        entity->visible = value;
        return true;
    }

    bool setPosition(EntityId id, Vec2 value) override {
        RuntimeEntity* entity = owner ? owner->findEntityMutable(id) : nullptr;
        if (!entity || !std::isfinite(value.x) || !std::isfinite(value.y)) return false;
        entity->transform.position = value;
        owner->refreshStaticCollider(id);
        return true;
    }

    bool isGrounded(EntityId id) override {
        const RuntimeEntity* entity = owner ? owner->findEntity(id) : nullptr;
        return entity && entity->platformerController && entity->platformerController->grounded;
    }
    bool requestPlatformerMove(EntityId id, float axis) override {
        RuntimeEntity* entity = owner ? owner->findEntityMutable(id) : nullptr;
        if (!entity || !entity->platformerController || !std::isfinite(axis)) return false;
        owner->platformerMoveIntents_[id] = std::clamp(axis, -1.f, 1.f);
        return true;
    }
    bool requestPlatformerJump(EntityId id) override {
        RuntimeEntity* entity = owner ? owner->findEntityMutable(id) : nullptr;
        if (!entity || !entity->platformerController) return false;
        owner->platformerJumpIntents_[id] = true;
        return true;
    }
    bool isObjectType(EntityId id, const ObjectTypeId& objectTypeId) override {
        const RuntimeEntity* entity = owner ? owner->findEntity(id) : nullptr;
        return entity && entity->objectTypeId == objectTypeId;
    }
    bool requestDestroy(EntityId id) override {
        RuntimeEntity* entity = owner ? owner->findEntityMutable(id) : nullptr;
        if (!entity || entity->destroyed) return false;
        owner->pendingDestroy_.insert(id);
        return true;
    }

    PlaySession* owner = nullptr;
};

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
    runtime.clips.reserve(asset.clips.size());
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        runtime.clips.push_back(RuntimeSpriteAnimationClip{
            clip.id,
            clip.imageId,
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

bool overlaps(const Aabb& a, const Aabb& b) {
    return a.minX < b.maxX && b.minX < a.maxX && a.minY < b.maxY && b.minY < a.maxY;
}

std::pair<EntityId, EntityId> collisionPair(EntityId a, EntityId b) {
    return a < b ? std::make_pair(a, b) : std::make_pair(b, a);
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

PlaySession::~PlaySession() = default;

PlaySession::PlaySession(PlaySession&& other) noexcept
    : scene_(std::move(other.scene_)),
      assets_(std::move(other.assets_)),
      spriteAnimationAssets_(std::move(other.spriteAnimationAssets_)),
      staticColliders_(std::move(other.staticColliders_)),
      logicHost_(std::move(other.logicHost_)),
      logicRuntime_(std::move(other.logicRuntime_)),
      platformerMoveIntents_(std::move(other.platformerMoveIntents_)),
      platformerJumpIntents_(std::move(other.platformerJumpIntents_)),
      logicScopesByEntity_(std::move(other.logicScopesByEntity_)),
      activeCollisionPairs_(std::move(other.activeCollisionPairs_)),
      pendingDestroy_(std::move(other.pendingDestroy_)) {
    if (logicHost_) logicHost_->owner = this;
}

PlaySession& PlaySession::operator=(PlaySession&& other) noexcept {
    if (this == &other) return *this;
    logicRuntime_.reset();
    logicHost_.reset();
    scene_ = std::move(other.scene_);
    assets_ = std::move(other.assets_);
    spriteAnimationAssets_ = std::move(other.spriteAnimationAssets_);
    staticColliders_ = std::move(other.staticColliders_);
    logicHost_ = std::move(other.logicHost_);
    logicRuntime_ = std::move(other.logicRuntime_);
    platformerMoveIntents_ = std::move(other.platformerMoveIntents_);
    platformerJumpIntents_ = std::move(other.platformerJumpIntents_);
    logicScopesByEntity_ = std::move(other.logicScopesByEntity_);
    activeCollisionPairs_ = std::move(other.activeCollisionPairs_);
    pendingDestroy_ = std::move(other.pendingDestroy_);
    if (logicHost_) logicHost_->owner = this;
    return *this;
}

std::optional<PlaySession> PlaySession::materialize(const ProjectDocument& document,
                                                    const SceneId& sceneId,
                                                    std::string* error) {
    // Compile every board before creating any runtime entity. A blocking
    // diagnostic rejects Play atomically and leaves authoring untouched.
    const Logic::LogicCompileResult logic = Logic::compileProjectLogic(document.data());
    if (!logic.ok()) {
        if (error) {
            *error = "Cannot start Play: Logic Board validation failed";
            if (!logic.diagnostics.empty()) {
                *error += " [" + logic.diagnostics.front().code + "] "
                       + logic.diagnostics.front().message;
            }
        }
        return std::nullopt;
    }

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

    // Structural order (SceneDef::instances' own): simulation must never be
    // coupled to the visual layer order (see RuntimeScene::renderOrder below).
    std::unordered_map<EntityId, std::size_t> indexByEntity;
    for (const SceneInstanceDef& instance : scene->instances) {
        RuntimeEntity entity;
        entity.id = instance.id;
        entity.objectTypeId = instance.objectTypeId;
        entity.name = instance.instanceName;
        entity.transform = instance.transform;
        const auto objectTypeIt = document.data().objectTypes.find(instance.objectTypeId);
        entity.visible = instance.visible
            && (objectTypeIt == document.data().objectTypes.end() || objectTypeIt->second.visible);
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
                StaticRuntimeCollider{entity.id, runtimeColliderBounds(entity), entity.collider->mode});
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
                // Preload every clip's sheet up front so a clip switch never does
                // file I/O inside the game loop (each clip owns its own image).
                for (const SpriteAnimationClipDef& c : animation->clips) {
                    const ImageAssetDef* clipImage = findImageAsset(document, c.imageId);
                    if (!clipImage) {
                        if (error) {
                            *error = "Cannot start Play: animation clip references missing image "
                                   + c.imageId;
                        }
                        return std::nullopt;
                    }
                    session.assets_.imageAssets.emplace(
                        clipImage->assetId,
                        RuntimeImageAsset{clipImage->assetId, clipImage->sourcePath});
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
                // The rendered image follows the active clip's own sheet.
                entity.sprite->assetId = clip->imageId;
                if (!clip->frames.empty()) {
                    entity.sprite->sourceRect = frameRect(clip->frames.front());
                    entity.sprite->hasSourceRect = true;
                }
                entity.spriteAnimator = std::move(animator);
            }
        }

        if (instance.tilemap.has_value()) {
            const TilesetAsset* tileset = document.findTilesetAsset(instance.tilemap->tilesetAssetId);
            if (!tileset) {
                if (error) {
                    *error = "Cannot start Play: tilemap references missing tileset "
                           + instance.tilemap->tilesetAssetId;
                }
                return std::nullopt;
            }
            const ImageAssetDef* tilesetImage = findImageAsset(document, tileset->imageAssetId);
            if (!tilesetImage) {
                if (error) {
                    *error = "Cannot start Play: tileset references missing image asset "
                           + tileset->imageAssetId;
                }
                return std::nullopt;
            }
            // Strict: an unresolvable tile id fails Play atomically rather
            // than silently starting with content the author placed missing
            // (Edit's own tilemapRenderCells stays lenient - see its doc).
            const std::optional<std::vector<TilemapResolvedCell>> resolved =
                resolveTilemapCellsStrict(*instance.tilemap, *tileset);
            if (!resolved) {
                if (error) {
                    *error = "Cannot start Play: tilemap entity " + std::to_string(instance.id)
                           + " references an unknown tile id";
                }
                return std::nullopt;
            }
            // Dedup is free: emplace on an already-present AssetId is a no-op,
            // so two tilemaps (or a tilemap and a sprite) sharing one image
            // asset still load exactly one texture.
            session.assets_.imageAssets.emplace(
                tilesetImage->assetId, RuntimeImageAsset{tilesetImage->assetId, tilesetImage->sourcePath});

            RuntimeTilemap runtimeTilemap;
            runtimeTilemap.imageAssetId = tileset->imageAssetId;
            runtimeTilemap.cellSize = instance.tilemap->cellSize;
            runtimeTilemap.cells.reserve(resolved->size());
            for (const TilemapResolvedCell& cell : *resolved) {
                runtimeTilemap.cells.push_back(RuntimeTilemapCell{
                    cell.cellX, cell.cellY,
                    AnimationFrameRect{cell.source.x, cell.source.y, cell.source.width, cell.source.height}});
            }
            entity.tilemap = std::move(runtimeTilemap);
        }

        indexByEntity.emplace(entity.id, session.scene().entities.size());
        session.scene().entities.push_back(std::move(entity));
    }

    // Render order is purely a draw-order hint for the Play snapshot, built
    // separately from the structural entities list above so a scene layer
    // reorder can never affect simulation order.
    for (const SceneInstanceDef* inst : document.instancesInRenderOrder(sceneId)) {
        const auto it = indexByEntity.find(inst->id);
        if (it != indexByEntity.end()) session.scene().renderOrder.push_back(it->second);
    }

    if (!logic.programs.empty()) {
        session.logicHost_ = std::make_unique<LogicHostAdapter>(&session);
        session.logicRuntime_ = std::make_unique<Logic::LogicRuntime>(*session.logicHost_);
        std::string logicError;
        if (!session.logicRuntime_->loadPrograms(logic.programs, &logicError)) {
            if (error) *error = "Cannot start Play: " + logicError;
            return std::nullopt;
        }
        for (const RuntimeEntity& entity : session.scene_.entities) {
            const auto typeIt = document.data().objectTypes.find(entity.objectTypeId);
            if (typeIt == document.data().objectTypes.end() || !typeIt->second.logicBoard) continue;
            const auto scope = session.logicRuntime_->install(entity.objectTypeId, entity.id, &logicError);
            if (!scope) {
                if (error) *error = "Cannot start Play: " + logicError;
                return std::nullopt;
            }
            session.logicScopesByEntity_.emplace(entity.id, *scope);
        }
        session.logicRuntime_->beginFrame();
        session.logicRuntime_->dispatchStart();
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
        if (entity.id == id && !entity.destroyed) return &entity;
    }
    return nullptr;
}

RuntimeEntity* PlaySession::findEntityMutable(EntityId id) {
    for (RuntimeEntity& entity : scene_.entities) {
        if (entity.id == id && !entity.destroyed) return &entity;
    }
    return nullptr;
}

void PlaySession::refreshStaticCollider(EntityId owner) {
    RuntimeEntity* entity = findEntityMutable(owner);
    if (!entity || !entity->collider) return;
    for (StaticRuntimeCollider& collider : staticColliders_) {
        if (collider.owner == owner) collider.bounds = runtimeColliderBounds(*entity);
    }
}

void PlaySession::dispatchCollisionTransitions() {
    if (!logicRuntime_) return;
    std::set<std::pair<EntityId, EntityId>> current;
    for (std::size_t i = 0; i < scene_.entities.size(); ++i) {
        const RuntimeEntity& a = scene_.entities[i];
        if (a.destroyed || !a.collider || !a.collider->enabled) continue;
        const Aabb aBounds = runtimeColliderBounds(a);
        for (std::size_t j = i + 1; j < scene_.entities.size(); ++j) {
            const RuntimeEntity& b = scene_.entities[j];
            if (b.destroyed || !b.collider || !b.collider->enabled) continue;
            if (overlaps(aBounds, runtimeColliderBounds(b))) current.insert(collisionPair(a.id, b.id));
        }
    }
    for (const auto& pair : current) {
        if (activeCollisionPairs_.count(pair) != 0) continue;
        logicRuntime_->dispatchCollisionEnter(pair.first, pair.second);
        logicRuntime_->dispatchCollisionEnter(pair.second, pair.first);
    }
    for (const auto& pair : activeCollisionPairs_) {
        if (current.count(pair) != 0) continue;
        logicRuntime_->dispatchCollisionExit(pair.first, pair.second);
        logicRuntime_->dispatchCollisionExit(pair.second, pair.first);
    }
    activeCollisionPairs_ = std::move(current);
}

void PlaySession::flushPendingDestroys() {
    if (pendingDestroy_.empty()) return;
    for (const EntityId id : pendingDestroy_) {
        RuntimeEntity* entity = findEntityMutable(id);
        if (!entity) continue;
        entity->destroyed = true;
        entity->visible = false;
        if (entity->collider) entity->collider->enabled = false;
        if (logicRuntime_) {
            const auto scope = logicScopesByEntity_.find(id);
            if (scope != logicScopesByEntity_.end()) logicRuntime_->cancelScope(scope->second);
        }
        logicScopesByEntity_.erase(id);
        platformerMoveIntents_.erase(id);
        platformerJumpIntents_.erase(id);
        staticColliders_.erase(std::remove_if(staticColliders_.begin(), staticColliders_.end(),
            [&](const StaticRuntimeCollider& collider) { return collider.owner == id; }),
            staticColliders_.end());
        for (auto it = activeCollisionPairs_.begin(); it != activeCollisionPairs_.end();) {
            if (it->first == id || it->second == id) it = activeCollisionPairs_.erase(it);
            else ++it;
        }
    }
    pendingDestroy_.clear();
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
        if (entity.destroyed) continue;
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
        // Texture follows the active clip's sheet (all clip images are preloaded),
        // so a future clip switch changes both the region and the image with no I/O.
        entity.sprite->assetId = clip->imageAssetId;
        entity.sprite->sourceRect = frameRect(clip->frames[index]);
        entity.sprite->hasSourceRect = true;
    }
    // Authored velocity (LinearMover) routed through the one resolver.
    for (RuntimeEntity& entity : scene_.entities) {
        if (entity.destroyed) continue;
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
    const bool logicJump = platformerJumpIntents_.count(entity.id) != 0;
    if ((input.jumpPressed || logicJump) && pc.grounded) {
        pc.verticalVelocity = -pc.jumpSpeed;   // -Y is up
        pc.grounded = false;
    }
    pc.verticalVelocity += pc.gravity * dt;    // +Y is down

    float axis = static_cast<float>(input.moveRight) - static_cast<float>(input.moveLeft);
    if (const auto it = platformerMoveIntents_.find(entity.id); it != platformerMoveIntents_.end())
        axis = it->second;
    const float dx = axis
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
    if (logicRuntime_) {
        platformerMoveIntents_.clear();
        platformerJumpIntents_.clear();
        logicRuntime_->beginFrame();
        for (LogicKey key : input.pressedLogicKeys) logicRuntime_->dispatchKeyPressed(key);
        for (LogicKey key : input.releasedLogicKeys) logicRuntime_->dispatchKeyReleased(key);
        for (LogicKey key : input.heldLogicKeys) logicRuntime_->dispatchKeyHeld(key);
        flushPendingDestroys();
    }
    if (!std::isfinite(dt) || dt <= 0.f) return;
    // One movement writer per entity (enforced at authoring): dispatch by driver.
    for (RuntimeEntity& entity : scene_.entities) {
        if (entity.destroyed) continue;
        if (entity.topDownController)        updateTopDown(entity, input, dt);
        else if (entity.platformerController) updatePlatformer(entity, input, dt);
    }
    dispatchCollisionTransitions();
    flushPendingDestroys();
}

} // namespace ArtCade::EditorNative
