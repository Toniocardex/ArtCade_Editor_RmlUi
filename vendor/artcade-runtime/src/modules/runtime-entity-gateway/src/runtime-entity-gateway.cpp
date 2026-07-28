#include "../include/runtime-entity-gateway.h"
#include "../include/physics-body-rules.h"
#include "entity-registry.h"
#include "collision-profile-resolve.h"
#include "../../scene-system/include/scene-lifecycle-service.h"
#include "../../scene-system/include/scene-manager.h"
#include "../../physics/include/physics.h"
#include "../../sprite-animator/include/sprite-animator.h"
#include "object-type-materialize.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace ArtCade::Modules {

namespace {

EntityDef cloneForSpawn(const EntityDef& proto, float x, float y) {
    EntityDef copy = proto;
    copy.id = 0;
    copy.transform.position = { x, y };
    copy.physics.physicsHandle = 0;
    copy.runtime.sceneActive = true;
    if (copy.sprite.alpha <= 0.f)
        copy.sprite.alpha = 1.f;
    return copy;
}

EntityDef minimalSpawnDef(const std::string& cls, float x, float y) {
    EntityDef def;
    def.id = 0;
    def.name = cls;
    def.className = cls;
    def.transform.position = { x, y };
    def.transform.scale = { 1.f, 1.f };
    def.runtime.sceneActive = true;
    return def;
}

constexpr float kDefaultColliderSize = 32.f;

bool hasExplicitColliderSize(const PhysicsComponent& comp) {
    return comp.collider.size.x > 2.f || comp.collider.size.y > 2.f;
}

Vec2 resolveWorldColliderSize(const Transform& transform,
                              const PhysicsComponent& comp,
                              bool hasExplicitCollider) {
    const Vec2 base = hasExplicitCollider
        ? comp.collider.size
        : Vec2{ kDefaultColliderSize, kDefaultColliderSize };
    if (hasExplicitCollider) {
        return {
            std::max(1.f, base.x),
            std::max(1.f, base.y),
        };
    }
    return {
        std::max(1.f, base.x * std::abs(transform.scale.x)),
        std::max(1.f, base.y * std::abs(transform.scale.y)),
    };
}

} // namespace

RuntimeEntityGateway::RuntimeEntityGateway(SceneManager& sm)
    : sceneManager_(sm),
      registry_(std::make_unique<EntityRegistry>()) {}

RuntimeEntityGateway::~RuntimeEntityGateway() = default;

bool RuntimeEntityGateway::init() { return true; }

void RuntimeEntityGateway::shutdown() {
    pendingDestroy_.clear();
    pendingSpawn_.clear();
    destroyBuffer_.clear();
    // Clear while physics is still alive: registry.clear() fires
    // on_destroy<PhysicsHandleComp> for every entity, which frees the
    // matching physics bodies. Detach physics afterwards so any stray
    // signal during ~EntityRegistry can't reach a torn-down module.
    registry_->clear();
    registry_->attachPhysicsModule(nullptr);
    lifecycleQueue_.clear();
    classPrototypes_.clear();
    spawnLogCallback_ = nullptr;
    // Drop the destroy hook BEFORE any further destroy() can fire. Owners
    // (e.g. World) wire a lambda capturing `this`; if their lifetime ends
    // before the gateway's, leaving the lambda live makes a subsequent
    // destroy(id) dereference dangling memory.
    destroyHandler_         = nullptr;
    createdHandler_         = nullptr;
    physicsTopologyHandler_ = nullptr;
    lifecycle_ = nullptr;
}

void RuntimeEntityGateway::setPhysics(Physics* physics) {
    physics_ = physics;
    registry_->attachPhysicsModule(physics);
}

void RuntimeEntityGateway::setSpriteAnimator(SpriteAnimator* animator) {
    spriteAnimator_ = animator;
}

void RuntimeEntityGateway::maybePlaySpawnClip(EntityId id) {
    if (!spriteAnimator_) return;

    SpriteRendererComponent renderer;
    SpriteAnimatorComponent animator;
    if (registry_->getSpriteRenderer(id, renderer)
        && registry_->getSpriteAnimator(id, animator)) {
        if (!std::isfinite(animator.playbackSpeed) || animator.playbackSpeed <= 0.f)
            return;
        // An animation owns a clip, while SpriteComponent owns the sheet that
        // the renderer can draw. Bind that sheet independently of Auto Play:
        // a stopped animation must still expose its first frame instead of
        // degrading to the generic entity placeholder.
        if (!animator.animationAssetId.empty() && !animator.defaultClipId.empty()) {
            const AssetId sheetAssetId = spriteAnimator_->clipAssetId(
                animator.animationAssetId, animator.defaultClipId);
            if (!sheetAssetId.empty()) {
                SpriteComponent sprite;
                if (registry_->getSprite(id, sprite)) {
                    sprite.spriteAssetId = sheetAssetId;
                    registry_->setSprite(id, sprite);
                }
            }
        }
        spriteAnimator_->setPlaybackSpeed(id, animator.playbackSpeed);
        if (animator.autoPlay && !animator.animationAssetId.empty()
            && !animator.defaultClipId.empty()) {
            if (!spriteAnimator_->play(
                    id, animator.animationAssetId, animator.defaultClipId)
                && spawnLogCallback_) {
                spawnLogCallback_(
                    "SpriteAnimator autoPlay failed: missing or empty clip '"
                    + animator.defaultClipId + "' on animation '"
                    + animator.animationAssetId + "' (entity "
                    + std::to_string(id) + ")");
            }
        }
        return;
    }

    SpriteComponent sprite;
    if (!getSprite(id, sprite) || !sprite.playClipOnSpawn || sprite.defaultClip.empty())
        return;
    if (spriteAnimator_->hasClip(sprite.defaultClip)) spriteAnimator_->play(id, sprite.defaultClip);
}

void RuntimeEntityGateway::replayActiveSpawnClips() {
    if (!spriteAnimator_) return;
    for (EntityId id : registry_->allIds()) {
        if (!registry_->sceneActive(id)) continue;
        maybePlaySpawnClip(id);
    }
}

void RuntimeEntityGateway::setSpawnLogCallback(SpawnLogCallback cb) {
    spawnLogCallback_ = std::move(cb);
}

void RuntimeEntityGateway::setEntityDestroyHandler(EntityDestroyHandler cb) {
    destroyHandler_ = std::move(cb);
}

void RuntimeEntityGateway::setEntityCreatedHandler(EntityCreatedHandler cb) {
    createdHandler_ = std::move(cb);
}

void RuntimeEntityGateway::setPhysicsTopologyHandler(PhysicsTopologyHandler cb) {
    physicsTopologyHandler_ = std::move(cb);
}

bool RuntimeEntityGateway::entityListedInActiveScene(EntityId id) const {
    const SceneDef* scene = sceneManager_.activeScene();
    if (!scene) return false;
    return std::find(scene->entityIds.begin(), scene->entityIds.end(), id)
           != scene->entityIds.end();
}

void RuntimeEntityGateway::captureSceneOwnershipFromScenes(
    const std::unordered_map<SceneId, SceneDef>& scenes)
{
    for (const auto& [sceneId, sceneDef] : scenes) {
        sceneAuthoredEntityIds_[sceneId] = sceneDef.entityIds;
        for (EntityId entityId : sceneDef.entityIds)
            entitySceneOwner_[entityId] = sceneId;
    }
}

void RuntimeEntityGateway::assignEntitySceneOwner(
    EntityId id, const SceneId& sceneId)
{
    if (id == 0 || sceneId.empty()) return;
    entitySceneOwner_[id] = sceneId;
}

std::vector<EntityId> RuntimeEntityGateway::entitiesOwnedByScene(
    const SceneId& sceneId) const
{
    std::vector<EntityId> out;
    for (const auto& [entityId, owner] : entitySceneOwner_) {
        if (owner == sceneId)
            out.push_back(entityId);
    }
    return out;
}

EntityId RuntimeEntityGateway::createAuthoredEntityForScene(
    EntityId authoredId, const EntityDef& def, const SceneId& sceneId)
{
    EntityDef copy = def;
    copy.runtime.sceneActive = false;
    copy.physics.physicsHandle = 0;
    const EntityId runtimeId = registry_->allocate(authoredId);
    copy.id = runtimeId;

    assignEntitySceneOwner(runtimeId, sceneId);
    registry_->setSceneActive(
        runtimeId, sceneManager_.activeSceneId() == sceneId);
    applyEntityDefToRegistry(runtimeId, copy);
    if (createdHandler_) createdHandler_(runtimeId, copy);

    if (registry_->sceneActive(runtimeId)) {
        ensurePhysicsBody(runtimeId);
        maybePlaySpawnClip(runtimeId);
    }
    return runtimeId;
}

bool RuntimeEntityGateway::restoreSceneFromAuthoring(const SceneId& sceneId) {
    if (!sceneManager_.getScene(sceneId)) return false;

    const auto authoredIt = sceneAuthoredEntityIds_.find(sceneId);
    if (authoredIt == sceneAuthoredEntityIds_.end()) return false;

    const std::vector<EntityId> toDestroy = entitiesOwnedByScene(sceneId);
    for (EntityId id : toDestroy)
        destroy(id);

    SceneDef* mutableScene = sceneManager_.getSceneMutable(sceneId);
    if (!mutableScene) return false;
    mutableScene->entityIds = authoredIt->second;

    for (EntityId authoredId : authoredIt->second) {
        const EntityDef* def = sceneManager_.getEntityDef(authoredId);
        if (!def) continue;
        createAuthoredEntityForScene(authoredId, *def, sceneId);
    }

    syncSceneActivation();
    return true;
}

bool RuntimeEntityGateway::isEntityActiveInScene(EntityId id) const {
    return registry_->contains(id) && registry_->sceneActive(id);
}

void RuntimeEntityGateway::ensurePhysicsBody(EntityId id) {
    if (!physics_) return;
    if (physicsHandle(id) != 0) return;

    PhysicsComponent comp{};
    if (!getPhysicsComponent(id, comp))
        return;

    Transform transform{};
    getTransform(id, transform);

    const bool hasExplicitCollider = hasExplicitColliderSize(comp);
    // ADR-0021 Slice 2: Top Down / Platformer do not invent a Physics body.
    // Locomotion solids come from BoxCollider2D → CollisionWorld. A Physics
    // body exists only when an explicit Physics collider size is authored.
    if (!hasExplicitCollider)
        return;

    PlatformerControllerComponent platformer{};
    const bool hasPlatformer = getPlatformerController(id, platformer);
    TopDownControllerComponent topDown{};
    const bool hasTopDown = getTopDownController(id, topDown);

    const EntityPhysicsFlags flags{
        hasExplicitCollider,
        hasPlatformer,
        hasTopDown,
    };
    const PhysicsBodyRules rules = resolvePhysicsBodyRules(comp, flags);
    applyPhysicsBodyRules(comp, rules);

    comp.collider.size = resolveWorldColliderSize(transform, comp, hasExplicitCollider);

    const uint32_t handle = physics_->createBody(id, comp);
    if (handle == 0) return;

    PhysicsComponent stored{};
    if (getPhysicsComponent(id, stored) && stored.bodyType != comp.bodyType) {
        stored.bodyType = comp.bodyType;
        registry_->setPhysics(id, stored);
    }
    setPhysicsHandle(id, handle);
    physics_->setPosition(handle, transform.position);
    physics_->setGravityScale(handle, rules.gravityScale);

    if (physicsTopologyHandler_)
        physicsTopologyHandler_();
}

void RuntimeEntityGateway::rebuildPhysicsBodyIfActive(EntityId id) {
    if (!physics_ || !isEntityActiveInScene(id)) return;
    teardownPhysicsBody(id);
    ensurePhysicsBody(id);
}

void RuntimeEntityGateway::teardownPhysicsBody(EntityId id) {
    const uint32_t handle = physicsHandle(id);
    if (!physics_ || handle == 0) return;
    physics_->destroyBody(handle);
    setPhysicsHandle(id, 0);
    if (physicsTopologyHandler_)
        physicsTopologyHandler_();
}

void RuntimeEntityGateway::deactivateEntity(EntityId id) {
    if (!registry_->contains(id)) return;
    registry_->setSceneActive(id, false);
    teardownPhysicsBody(id);
}

void RuntimeEntityGateway::activateEntity(EntityId id) {
    if (!registry_->contains(id)) return;
    const bool wasActive = registry_->sceneActive(id);
    registry_->setSceneActive(id, true);
    ensurePhysicsBody(id);
    if (!wasActive) {
        maybePlaySpawnClip(id);
    }
}

void RuntimeEntityGateway::syncSceneActivation() {
    for (EntityId id : allIds()) {
        if (entityListedInActiveScene(id))
            activateEntity(id);
        else
            deactivateEntity(id);
    }
}

void RuntimeEntityGateway::applyEntityDefToRegistry(
    EntityId id, const EntityDef& def)
{
    // Order matters: setIdentity is LAST so that the on_construct<Identity>
    // signal observes the fully-populated entity (Transform, Sprite, …)
    // when emitting LifecycleEvent::Spawned. This lets future signal
    // handlers read sibling components without races.
    registry_->setPhysicsHandle(id, 0);
    registry_->setVisibleInGame(id, def.visible);
    // Decouple facing from scale: a negative authored scale becomes a flip flag
    // so scale stays pure magnitude. Flip is then driven only by the flags, and
    // Set Scale at runtime can no longer clobber a logic-set facing.
    Transform transform = def.transform;
    SpriteComponent sprite = def.sprite;
    sprite.layerId = def.layerId;
    if (transform.scale.x < 0.f) { sprite.flipX = !sprite.flipX; transform.scale.x = -transform.scale.x; }
    if (transform.scale.y < 0.f) { sprite.flipY = !sprite.flipY; transform.scale.y = -transform.scale.y; }
    registry_->setTransform(id, transform);
    if (!def.visible)
        sprite.alpha = 0.f;
    registry_->setSprite(id, sprite);
    registry_->setSpriteRenderer(id, def.spriteRenderer);
    registry_->setSpriteAnimator(id, def.spriteAnimator);
    registry_->setPhysics(id, def.physics);
    // ADR-0014: session CollisionBody is derived only from BoxCollider2D.
    // Authored EntityDef.collisionBody is ignored (technical debt / legacy JSON).
    registry_->setCollisionBody(id, ArtCade::materializeBoxCollider2D(def));
    registry_->setPlatformer(id, def.platformerController);
    registry_->setTopDown(id, def.topDownController);
    if (def.linearMover) {
        LinearMoverComponent lm = *def.linearMover;
        LinearMoverComponent prev{};
        if (registry_->getLinearMover(id, prev))
            lm._paused = prev._paused;
        registry_->setLinearMover(id, lm);
    } else {
        registry_->setLinearMover(id, std::nullopt);
    }
    registry_->setCameraTarget(id, def.cameraTarget);
    if (def.magneticItem) {
        MagneticItemComponent mi = *def.magneticItem;
        MagneticItemComponent prev{};
        if (registry_->getMagneticItem(id, prev))
            mi._enabled = prev._enabled;
        registry_->setMagneticItem(id, mi);
    } else {
        registry_->setMagneticItem(id, std::nullopt);
    }
    registry_->setHordeMember(id, def.hordeMember);
    if (def.autoDestroy) {
        AutoDestroyComponent ad = *def.autoDestroy;
        AutoDestroyComponent prev{};
        if (registry_->getAutoDestroy(id, prev))
            ad._timeAlive = prev._timeAlive;
        registry_->setAutoDestroy(id, ad);
    } else {
        registry_->setAutoDestroy(id, std::nullopt);
    }
    registry_->setText(id, def.text);
    registry_->setGauge(id, def.gauge);
    registry_->setTilemap(id, def.tilemap);
    registry_->setDialog(id, def.dialog);
    registry_->setIdentity(id, def.className, def.tags);
}

EntityId RuntimeEntityGateway::create(const EntityDef& def) {
    EntityDef copy = def;
    copy.physics.physicsHandle = 0;
    const EntityId id = registry_->allocate(copy.id);
    copy.id = id;

    registry_->setSceneActive(id, entityListedInActiveScene(id));
    applyEntityDefToRegistry(id, copy);
    if (createdHandler_) createdHandler_(id, copy);

    if (const SceneId owner = sceneManager_.activeSceneId(); !owner.empty()) {
        assignEntitySceneOwner(id, owner);
        if (SceneDef* scene = sceneManager_.activeSceneMutable()) {
            if (std::find(scene->entityIds.begin(), scene->entityIds.end(), id)
                == scene->entityIds.end())
                scene->entityIds.push_back(id);
        }
    }

    if (registry_->sceneActive(id)) {
        ensurePhysicsBody(id);
        maybePlaySpawnClip(id);
    }
    return id;
}

void RuntimeEntityGateway::rebuildClassPrototypes(
    const std::unordered_map<EntityId, EntityDef>& entityDefs,
    const std::unordered_map<std::string, EntityDef>* objectTypes)
{
    const std::unordered_map<std::string, EntityDef> empty;
    ArtCade::rebuildClassPrototypes(
        classPrototypes_,
        objectTypes ? *objectTypes : empty,
        entityDefs);
}

EntityId RuntimeEntityGateway::spawnFromClass(const std::string& className, float x, float y) {
    EntityDef copy;
    auto protoIt = classPrototypes_.find(className);
    if (protoIt != classPrototypes_.end()) {
        copy = cloneForSpawn(protoIt->second, x, y);
    } else {
        copy = minimalSpawnDef(className, x, y);
    }

    const EntityId id = registry_->allocate(copy.id);
    copy.id = id;
    applyEntityDefToRegistry(id, copy);
    if (createdHandler_) createdHandler_(id, copy);

    if (SceneDef* scene = sceneManager_.activeSceneMutable()) {
        if (std::find(scene->entityIds.begin(), scene->entityIds.end(), id)
            == scene->entityIds.end())
            scene->entityIds.push_back(id);
    }
    assignEntitySceneOwner(id, sceneManager_.activeSceneId());
    activateEntity(id);

    char buf[96];
    std::snprintf(buf, sizeof(buf), "[Spawn] %s #%u at (%.0f, %.0f)",
                  className.c_str(), static_cast<unsigned>(id), x, y);
    const std::string line(buf);
    if (spawnLogCallback_)
        spawnLogCallback_(line);
    else
        std::cout << line << std::endl;

    return id;
}

void RuntimeEntityGateway::destroy(EntityId id) {
    // Fire the upstream destroy hook BEFORE erasing so callers can still
    // inspect the live entity (e.g. World scrubbing per-entity maps keyed
    // by id). EnTT will happily recycle the id on the next create(),
    // so anything not cleaned here leaks across recycled lifetimes.
    if (destroyHandler_) destroyHandler_(id);

    entitySceneOwner_.erase(id);
    sceneManager_.removeEntityFromAllScenes(id);
    // No explicit teardownPhysicsBody: registry_->erase fires the
    // on_destroy<PhysicsHandleComp> signal which frees the physics body
    // (see EntityRegistry::Impl::onPhysicsHandleDestroyed). It also
    // fires on_destroy<Identity>, draining class/tag indices and
    // queueing a LifecycleEvent::Destroyed for Lua.
    registry_->erase(id);
}

void RuntimeEntityGateway::drainLifecycleEvents(
    std::vector<LifecycleEvent>& out)
{
    // First make sure local + registry queues are merged: gateway-level
    // operations (queueDestroy → flushPendingOperations → destroy) already
    // route through the registry signals, so the registry queue is the
    // single source of truth. We keep the gateway-level vector member
    // around for future extension (e.g. non-component lifecycle hooks).
    registry_->drainLifecycleEvents(lifecycleQueue_);
    if (lifecycleQueue_.empty()) return;
    out.insert(out.end(),
               std::make_move_iterator(lifecycleQueue_.begin()),
               std::make_move_iterator(lifecycleQueue_.end()));
    lifecycleQueue_.clear();
}

void RuntimeEntityGateway::queueDestroy(EntityId id) {
    if (id == 0) return;
    // Don't pay an O(n) std::find per enqueue: the flush loop dedups via
    // exists() so a duplicate queued id becomes a cheap no-op there.
    // This turned death-wave queueing from O(n²) into O(n).
    pendingDestroy_.push_back(id);
}

EntityId RuntimeEntityGateway::queueSpawn(const EntityDef& def) {
    pendingSpawn_.push_back(def);
    return def.id != 0 ? def.id : 0;
}

void RuntimeEntityGateway::flushPendingOperations() {
    // Drain destroys via swap-and-iterate, looping until stable. The previous
    // range-for over `pendingDestroy_` invalidated its own iterator if any
    // destroy callback (EnTT signal → Lua → queueDestroy of a related entity)
    // grew the vector mid-iteration. Swapping into a local batch makes new
    // pushes land on the empty source vector instead, and the while-loop
    // catches them on the next iteration so cascading destroys still finish
    // in the same flush call (not deferred to the next frame).
    //
    // exists(id) absorbs duplicates: the same id queued twice (or queued
    // again from a callback fired by its own destroy) simply skips the
    // second pass, which would otherwise be UB inside EnTT::erase.
    while (!pendingDestroy_.empty()) {
        std::vector<EntityId> batch;
        batch.swap(pendingDestroy_);
        for (EntityId id : batch) {
            if (!exists(id)) continue;
            destroyBuffer_.push_back({ id });
            destroy(id);
        }
    }

    // Spawns get the same swap-and-iterate treatment: a spawn callback that
    // queues another spawn (e.g. spawn-on-spawn chains) is processed in the
    // same flush, and we never iterate a mutating vector.
    while (!pendingSpawn_.empty()) {
        std::vector<EntityDef> batch;
        batch.swap(pendingSpawn_);
        for (const EntityDef& def : batch)
            create(def);
    }
}

bool RuntimeEntityGateway::exists(EntityId id) const {
    return registry_->contains(id);
}

std::string RuntimeEntityGateway::className(EntityId id) const {
    if (!registry_->contains(id)) return {};
    return registry_->className(id);
}

const EntityDef* RuntimeEntityGateway::getEntityDef(EntityId id) const {
    return sceneManager_.getEntityDef(id);
}

bool RuntimeEntityGateway::getTransform(EntityId id, Transform& out) const {
    return registry_->getTransform(id, out);
}

bool RuntimeEntityGateway::getAuthoringTransform(EntityId id, Transform& out) const {
    if (getTransform(id, out))
        return true;
    if (const EntityDef* def = sceneManager_.getEntityDef(id)) {
        out = def->transform;
        return true;
    }
    return false;
}

bool RuntimeEntityGateway::setTransform(EntityId id, const Transform& transform) {
    if (!registry_->contains(id)) return false;
    Transform previous{};
    const bool hadPrevious = getTransform(id, previous);
    registry_->setTransform(id, transform);
    if (physics_) {
        const uint32_t handle = physicsHandle(id);
        if (handle != 0) {
            const bool scaleChanged = hadPrevious &&
                (previous.scale.x != transform.scale.x ||
                 previous.scale.y != transform.scale.y);
            if (scaleChanged)
                rebuildPhysicsBodyIfActive(id);
            else
                physics_->setPosition(handle, transform.position);
        }
    }
    return true;
}

bool RuntimeEntityGateway::setTransform(EntityId id, Vec2 position, float rotation, Vec2 scale) {
    Transform transform{};
    if (!getTransform(id, transform)) return false;
    transform.position = position;
    transform.rotation = rotation;
    transform.scale    = scale;
    return setTransform(id, transform);
}

bool RuntimeEntityGateway::getSprite(EntityId id, SpriteComponent& out) const {
    return registry_->getSprite(id, out);
}

bool RuntimeEntityGateway::setSprite(EntityId id, const SpriteComponent& sprite) {
    if (!registry_->contains(id)) return false;
    registry_->setSprite(id, sprite);
    return true;
}

bool RuntimeEntityGateway::getSpriteRenderer(EntityId id, SpriteRendererComponent& out) const {
    return registry_->getSpriteRenderer(id, out);
}

bool RuntimeEntityGateway::getSpriteAnimator(EntityId id, SpriteAnimatorComponent& out) const {
    return registry_->getSpriteAnimator(id, out);
}

bool RuntimeEntityGateway::getPhysicsComponent(EntityId id, PhysicsComponent& out) const {
    return registry_->getPhysics(id, out);
}

bool RuntimeEntityGateway::setPhysicsComponent(EntityId id, const PhysicsComponent& physics) {
    if (!registry_->contains(id)) return false;
    const bool active = isEntityActiveInScene(id);
    if (active)
        teardownPhysicsBody(id);
    registry_->setPhysics(id, physics);
    if (active)
        ensurePhysicsBody(id);
    return true;
}

bool RuntimeEntityGateway::getPlatformerController(
    EntityId id, PlatformerControllerComponent& out) const
{
    return registry_->getPlatformer(id, out);
}

bool RuntimeEntityGateway::setPlatformerController(
    EntityId id, const std::optional<PlatformerControllerComponent>& controller)
{
    if (!registry_->contains(id)) return false;
    registry_->setPlatformer(id, controller);
    rebuildPhysicsBodyIfActive(id);
    return true;
}

bool RuntimeEntityGateway::getTopDownController(
    EntityId id, TopDownControllerComponent& out) const
{
    return registry_->getTopDown(id, out);
}

bool RuntimeEntityGateway::setTopDownController(
    EntityId id, const std::optional<TopDownControllerComponent>& controller)
{
    if (!registry_->contains(id)) return false;
    registry_->setTopDown(id, controller);
    rebuildPhysicsBodyIfActive(id);
    return true;
}

bool RuntimeEntityGateway::getLinearMover(EntityId id, LinearMoverComponent& out) const {
    return registry_->getLinearMover(id, out);
}

bool RuntimeEntityGateway::setLinearMover(
    EntityId id, const std::optional<LinearMoverComponent>& mover)
{
    if (!registry_->contains(id)) return false;
    registry_->setLinearMover(id, mover);
    return true;
}

bool RuntimeEntityGateway::getCameraTarget(
    EntityId id, CameraTargetComponent& out) const
{
    return registry_->getCameraTarget(id, out);
}

bool RuntimeEntityGateway::setCameraTarget(
    EntityId id, const std::optional<CameraTargetComponent>& target)
{
    if (!registry_->contains(id)) return false;
    registry_->setCameraTarget(id, target);
    return true;
}

bool RuntimeEntityGateway::getMagneticItem(
    EntityId id, MagneticItemComponent& out) const
{
    return registry_->getMagneticItem(id, out);
}

bool RuntimeEntityGateway::setMagneticItem(
    EntityId id, const std::optional<MagneticItemComponent>& item)
{
    if (!registry_->contains(id)) return false;
    registry_->setMagneticItem(id, item);
    return true;
}

bool RuntimeEntityGateway::getHordeMember(
    EntityId id, HordeMemberComponent& out) const
{
    return registry_->getHordeMember(id, out);
}

bool RuntimeEntityGateway::setHordeMember(
    EntityId id, const std::optional<HordeMemberComponent>& horde)
{
    if (!registry_->contains(id)) return false;
    registry_->setHordeMember(id, horde);
    return true;
}

bool RuntimeEntityGateway::getAutoDestroy(EntityId id, AutoDestroyComponent& out) const {
    return registry_->getAutoDestroy(id, out);
}

bool RuntimeEntityGateway::setAutoDestroy(
    EntityId id, const std::optional<AutoDestroyComponent>& autoDestroy)
{
    if (!registry_->contains(id)) return false;
    registry_->setAutoDestroy(id, autoDestroy);
    return true;
}

bool RuntimeEntityGateway::getText(EntityId id, TextComponent& out) const {
    return registry_->getText(id, out);
}

bool RuntimeEntityGateway::setText(
    EntityId id, const std::optional<TextComponent>& text)
{
    if (!registry_->contains(id)) return false;
    registry_->setText(id, text);
    return true;
}

bool RuntimeEntityGateway::getGauge(EntityId id, GaugeComponent& out) const {
    return registry_->getGauge(id, out);
}

bool RuntimeEntityGateway::setGauge(
    EntityId id, const std::optional<GaugeComponent>& gauge)
{
    if (!registry_->contains(id)) return false;
    registry_->setGauge(id, gauge);
    return true;
}

bool RuntimeEntityGateway::getTilemap(EntityId id, TilemapComponent& out) const {
    return registry_->getTilemap(id, out);
}

bool RuntimeEntityGateway::getCollisionBody(EntityId id, CollisionBodyComponent& out) const {
    return registry_->getCollisionBody(id, out);
}

bool RuntimeEntityGateway::getResolvedCollisionBody(
    EntityId id,
    CollisionBodyComponent& out) const
{
    CollisionBodyComponent authored{};
    Transform transform{};
    SpriteComponent sprite{};
    if (!registry_->getCollisionBody(id, authored)) return false;
    if (!registry_->getTransform(id, transform)) return false;
    registry_->getSprite(id, sprite);
    return CollisionProfileResolve::resolve_collision_body(
        id, sprite, transform, authored, collisionProfiles_,
        spritePathToAssetId_, spriteAnimator_, out);
}

bool RuntimeEntityGateway::setCollisionBody(
    EntityId id,
    const std::optional<CollisionBodyComponent>& collisionBody) {
    if (!registry_->contains(id)) return false;
    registry_->setCollisionBody(id, collisionBody);
    rebuildPhysicsBodyIfActive(id);
    return true;
}

bool RuntimeEntityGateway::getDialog(EntityId id, DialogComponent& out) const {
    return registry_->getDialog(id, out);
}

bool RuntimeEntityGateway::setDialog(
    EntityId id, const std::optional<DialogComponent>& dialog)
{
    if (!registry_->contains(id)) return false;
    registry_->setDialog(id, dialog);
    return true;
}

size_t RuntimeEntityGateway::activeSceneEntityCount() const {
    size_t n = 0;
    for (EntityId id : registry_->allIds()) {
        if (isEntityActiveInScene(id)) ++n;
    }
    return n;
}

size_t RuntimeEntityGateway::activePhysicsBodyCount() const {
    size_t n = 0;
    for (EntityId id : registry_->allIds()) {
        if (isEntityActiveInScene(id) && physicsHandle(id) != 0) ++n;
    }
    return n;
}

uint32_t RuntimeEntityGateway::physicsHandle(EntityId id) const {
    return registry_->physicsHandle(id);
}

bool RuntimeEntityGateway::hasPhysicsBody(EntityId id) const {
    return physicsHandle(id) != 0;
}

void RuntimeEntityGateway::setPhysicsHandle(EntityId id, uint32_t handle) {
    if (!registry_->contains(id)) return;
    registry_->setPhysicsHandle(id, handle);
}

std::vector<EntityId> RuntimeEntityGateway::poolByClass(const std::string& className) const {
    const std::vector<EntityId>& ids = registry_->idsByClass(className);
    std::vector<EntityId> out;
    out.reserve(ids.size());
    for (EntityId id : ids) {
        if (isEntityActiveInScene(id))
            out.push_back(id);
    }
    return out;
}

size_t RuntimeEntityGateway::poolCount(const std::string& className) const {
    size_t count = 0;
    for (EntityId id : registry_->idsByClass(className)) {
        if (isEntityActiveInScene(id))
            ++count;
    }
    return count;
}

std::vector<EntityId> RuntimeEntityGateway::byTag(const std::string& tag) const {
    std::vector<EntityId> out;
    out.reserve(registry_->idsByTag(tag).size());
    for (EntityId id : registry_->idsByTag(tag)) {
        if (isEntityActiveInScene(id))
            out.push_back(id);
    }
    return out;
}

void RuntimeEntityGateway::forEachActiveByTag(
    const std::string& tag,
    const ActiveByTagFn& fn) const
{
    for (EntityId id : registry_->idsByTag(tag)) {
        if (isEntityActiveInScene(id))
            fn(id);
    }
}

std::vector<EntityId> RuntimeEntityGateway::allIds() const {
    return registry_->allIds();
}

const std::vector<EntityId>& RuntimeEntityGateway::persistentEntityIds() const {
    return persistentEntityIds_;
}

void RuntimeEntityGateway::forEachActiveRenderable(
    const ActiveRenderableFn& fn) const
{
    registry_->forEachActiveRenderable(fn);
}

void RuntimeEntityGateway::forEachActivePhysicsBody(
    const ActivePhysicsBodyFn& fn)
{
    registry_->forEachActivePhysicsBody(fn);
}

void RuntimeEntityGateway::forEachActiveCollisionBody(
    const ActiveCollisionBodyFn& fn) const
{
    registry_->forEachActiveCollisionBody(
        [this, &fn](EntityId id, const Transform& transform, const CollisionBodyComponent& body) {
            CollisionBodyComponent resolved{};
            SpriteComponent sprite{};
            registry_->getSprite(id, sprite);
            if (!CollisionProfileResolve::resolve_collision_body(
                    id, sprite, transform, body, collisionProfiles_,
                    spritePathToAssetId_, spriteAnimator_, resolved))
                return;
            if (!resolved.enabled) return;
            fn(id, transform, resolved);
        });
}

void RuntimeEntityGateway::setCollisionProjectData(
    std::vector<PhysicsLayerDef> layers,
    std::unordered_map<std::string, CollisionProfileDef> profiles,
    std::unordered_map<std::string, std::string> spritePathToAssetId)
{
    physicsLayers_ = std::move(layers);
    collisionProfiles_ = std::move(profiles);
    spritePathToAssetId_ = std::move(spritePathToAssetId);
}

const std::vector<PhysicsLayerDef>& RuntimeEntityGateway::physicsLayers() const {
    return physicsLayers_;
}

void RuntimeEntityGateway::set_scene_lifecycle_service(
    SceneLifecycleService* lifecycle) {
    lifecycle_ = lifecycle;
}

void RuntimeEntityGateway::forEachActivePlatformer(
    const ActivePlatformerFn& fn) const
{
    registry_->forEachActivePlatformer(fn);
}

void RuntimeEntityGateway::forEachActiveTopDown(
    const ActiveTopDownFn& fn) const
{
    registry_->forEachActiveTopDown(fn);
}

void RuntimeEntityGateway::forEachActiveLinearMover(
    const ActiveLinearMoverFn& fn) const
{
    registry_->forEachActiveLinearMover(fn);
}

void RuntimeEntityGateway::forEachActiveCameraTarget(
    const ActiveCameraTargetFn& fn) const
{
    registry_->forEachActiveCameraTarget(fn);
}

void RuntimeEntityGateway::forEachActiveMagneticItem(
    const ActiveMagneticItemFn& fn) const
{
    registry_->forEachActiveMagneticItem(fn);
}

void RuntimeEntityGateway::forEachActiveHordeMember(
    const ActiveHordeMemberFn& fn) const
{
    registry_->forEachActiveHordeMember(fn);
}

void RuntimeEntityGateway::forEachActiveAutoDestroy(
    const ActiveAutoDestroyFn& fn)
{
    registry_->forEachActiveAutoDestroy(fn);
}

std::vector<EntityId> RuntimeEntityGateway::activeSceneIds() const {
    const SceneDef* scene = sceneManager_.activeScene();
    if (!scene) return {};
    std::vector<EntityId> out;
    out.reserve(scene->entityIds.size());
    for (EntityId id : scene->entityIds) {
        if (isEntityActiveInScene(id))
            out.push_back(id);
    }
    return out;
}

void RuntimeEntityGateway::registerScenes(
    const std::unordered_map<SceneId, SceneDef>& scenes,
    const std::unordered_map<EntityId, EntityDef>& /*entityDefs*/)
{
    sceneManager_.registerScenes(scenes, {});
    captureSceneOwnershipFromScenes(scenes);
}

bool RuntimeEntityGateway::replaceProject(
    const std::unordered_map<SceneId, SceneDef>& scenes,
    const std::unordered_map<EntityId, EntityDef>& entityDefs,
    const SceneId& activeSceneId,
    const std::unordered_map<std::string, EntityDef>* objectTypes)
{
    // registry_->clear() fires on_destroy<PhysicsHandleComp> for every
    // entity, which calls physics_->destroyBody on each live handle.
    // No explicit destroyAllBodies() is needed — the signal is the
    // single source of truth for physics body teardown. Keeping a parallel
    // batch call would risk double-free if the wrapper's destroyAll
    // and per-handle destroy interact.
    if (destroyHandler_) {
        for (EntityId id : registry_->allIds()) destroyHandler_(id);
    }
    registry_->clear();
    persistentEntityIds_.clear();
    entitySceneOwner_.clear();
    sceneAuthoredEntityIds_.clear();
    rebuildClassPrototypes(entityDefs, objectTypes);
    sceneManager_.registerScenes(scenes, entityDefs);
    captureSceneOwnershipFromScenes(scenes);
    for (const auto& [id, def] : entityDefs) {
        EntityDef copy = def;
        copy.runtime.sceneActive = false;
        copy.physics.physicsHandle = 0;
        const EntityId runtimeId = registry_->allocate(copy.id);
        copy.id = runtimeId;

        registry_->setSceneActive(runtimeId, false);
        applyEntityDefToRegistry(runtimeId, copy);
        persistentEntityIds_.push_back(runtimeId);
        if (createdHandler_) createdHandler_(runtimeId, copy);
    }

    if (!activeSceneId.empty())
        sceneManager_.loadScene(activeSceneId);
    else if (!scenes.empty())
        sceneManager_.loadScene(scenes.begin()->first);

    syncSceneActivation();
    return true;
}

bool RuntimeEntityGateway::updateEntity(EntityId id, const EntityDef& def) {
    if (!exists(id)) return false;

    const bool wasActive = registry_->sceneActive(id);
    const uint32_t oldHandle = physicsHandle(id);
    if (oldHandle != 0)
        teardownPhysicsBody(id);

    EntityDef copy = def;
    copy.id = id;
    copy.physics.physicsHandle = 0;

    applyEntityDefToRegistry(id, copy);
    sceneManager_.upsertEntityDef(id, copy);

    if (!copy.className.empty()) {
        auto it = classPrototypes_.find(copy.className);
        if (it == classPrototypes_.end())
            classPrototypes_[copy.className] = copy;
    }

    if (wasActive) {
        ensurePhysicsBody(id);
    }
    return true;
}

void RuntimeEntityGateway::setTilesets(std::vector<TilesetAsset> tilesets) {
    sceneManager_.setTilesets(std::move(tilesets));
}

float RuntimeEntityGateway::tilesetTileSize(const std::string& assetId) const {
    if (assetId.empty()) return 0.f;
    for (const auto& ts : sceneManager_.tilesets()) {
        if (ts.assetId == assetId)
            return static_cast<float>(ts.slicing.tileWidth);
    }
    return 0.f;
}

void RuntimeEntityGateway::setSceneLayers(std::vector<SceneLayerDef> layers) {
    sceneManager_.setSceneLayers(std::move(layers));
}

const std::vector<SceneLayerDef>& RuntimeEntityGateway::sceneLayers() const {
    return sceneManager_.sceneLayers();
}

bool RuntimeEntityGateway::loadScene(const SceneId& id) {
    if (lifecycle_) return lifecycle_->load_immediate(id).changed;
    if (!sceneManager_.loadScene(id)) return false;
    syncSceneActivation();
    return true;
}

void RuntimeEntityGateway::requestLoadScene(const SceneId& id, float fadeSeconds) {
    if (lifecycle_) {
        lifecycle_->request_load(id, fadeSeconds);
        return;
    }
    if (fadeSeconds <= 0.f) {
        loadScene(id);
        return;
    }
    loadScene(id);
}

void RuntimeEntityGateway::requestSceneReactivation(float fadeSeconds) {
    if (lifecycle_) {
        lifecycle_->request_reactivate(fadeSeconds);
        return;
    }
    loadScene(activeSceneId());
}

void RuntimeEntityGateway::requestRestartScene(float fadeSeconds) {
    if (lifecycle_) {
        lifecycle_->request_restart(fadeSeconds);
        return;
    }
    if (!restoreSceneFromAuthoring(activeSceneId())) return;
    syncSceneActivation();
}

void RuntimeEntityGateway::tickSceneTransition(float dt) {
    if (lifecycle_) lifecycle_->tick(dt);
}

float RuntimeEntityGateway::sceneFadeAlpha() const {
    if (lifecycle_) return lifecycle_->scene_fade_alpha();
    return 0.f;
}

std::vector<RuntimeEntityGateway::DestroyedEvent>
RuntimeEntityGateway::pollDestroyed() {
    std::vector<DestroyedEvent> out;
    out.swap(destroyBuffer_);
    return out;
}

SceneId RuntimeEntityGateway::activeSceneId() const {
    return sceneManager_.activeSceneId();
}

const SceneDef* RuntimeEntityGateway::activeScene() const {
    return sceneManager_.activeScene();
}

SceneDef* RuntimeEntityGateway::activeSceneMutable() {
    return sceneManager_.activeSceneMutable();
}

bool RuntimeEntityGateway::visibleInGame(EntityId id) const {
    return registry_->visibleInGame(id);
}

bool RuntimeEntityGateway::setRuntimeVisible(EntityId id, bool visible) {
    if (!registry_->contains(id)) return false;
    registry_->setVisibleInGame(id, visible);
    SpriteComponent sprite{};
    if (!registry_->getSprite(id, sprite)) return true;
    if (!visible) {
        sprite.alpha = 0.f;
    } else if (const EntityDef* def = sceneManager_.getEntityDef(id)) {
        sprite.alpha = def->sprite.alpha;
    }
    registry_->setSprite(id, sprite);
    return true;
}

void RuntimeEntityGateway::applyDesignVisibilityForPlay() {
    for (const EntityId id : activeSceneIds()) {
        SpriteComponent sprite{};
        if (!registry_->getSprite(id, sprite)) continue;
        if (!registry_->visibleInGame(id)) {
            sprite.alpha = 0.f;
        } else if (const EntityDef* def = sceneManager_.getEntityDef(id)) {
            sprite.alpha = def->sprite.alpha;
        }
        registry_->setSprite(id, sprite);
    }
}

void RuntimeEntityGateway::restoreDesignVisibilityForEdit() {
    for (const EntityId id : activeSceneIds()) {
        const EntityDef* def = sceneManager_.getEntityDef(id);
        if (!def) continue;
        registry_->setSprite(id, def->sprite);
    }
}

void RuntimeEntityGateway::forEachActiveHiddenInGame(
    const ActiveHiddenInGameFn& fn) const
{
    registry_->forEachActiveHiddenInGame(fn);
}

} // namespace ArtCade::Modules
