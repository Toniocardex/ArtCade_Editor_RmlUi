#pragma once

#include "../../../core/types.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::Modules {

class Physics;

/**
 * EntityRegistry — internal storage abstraction for RuntimeEntityGateway.
 *
 * Backed by an `entt::registry` (PIMPL — the EnTT header stays out of this
 * include surface). Owns the runtime component bag (transform / sprite /
 * physics / sensor / platformer / autoDestroy / physicsHandle), the scene
 * activation tag, and the className / tag indexes that back the pool /
 * by-tag queries.
 *
 * The gateway is the ONLY consumer — this header lives under `src/` so it
 * is not part of the module's public include surface.
 *
 * Determinism: className / tag indexes are maintained manually (push_back
 * in insertion order) because `entt::registry`'s views do not guarantee a
 * stable order across runs and ArtCade games are deterministic via Lua.
 *
 * EntityId ↔ entt::entity mapping is kept in the impl (the loader can
 * request "preserve this id" via allocate(hint) for ids coming from the
 * project JSON, which entt would assign differently on its own).
 */
class EntityRegistry final {
public:
    EntityRegistry();
    ~EntityRegistry();

    EntityRegistry(const EntityRegistry&)            = delete;
    EntityRegistry& operator=(const EntityRegistry&) = delete;

    // ---- Records --------------------------------------------------------

    /** Allocate a fresh EntityId or honour `hint` if non-zero and not yet
     *  in use. The next free id is always bumped past any allocated/hinted
     *  id so subsequent allocate(0) calls never alias an existing one. */
    EntityId allocate(EntityId hint = 0);
    /** Ensure a record exists for `id` (idempotent). Returns true if a new
     *  record was inserted, false if one was already there. */
    bool touch(EntityId id);
    /** Drop the record and any index references. */
    void erase(EntityId id);
    bool contains(EntityId id) const;
    void clear();

    std::vector<EntityId> allIds() const;

    // ---- Identity + indexes --------------------------------------------

    /** Set className + tags for `id`, refreshing both indexes. */
    void setIdentity(EntityId id, std::string className,
                     std::vector<std::string> tags);

    const std::string& className(EntityId id) const;
    const std::vector<std::string>& tags(EntityId id) const;

    /** Insertion-order list of ids carrying `className`. */
    /** Live view into the class index; empty vector when unknown. */
    const std::vector<EntityId>& idsByClass(const std::string& className) const;
    /** Insertion-order list of ids carrying `tag`. */
    const std::vector<EntityId>& idsByTag(const std::string& tag) const;

    // ---- Scene activation ----------------------------------------------

    bool sceneActive(EntityId id) const;
    void setSceneActive(EntityId id, bool active);

    bool visibleInGame(EntityId id) const;
    void setVisibleInGame(EntityId id, bool visible);

    // ---- Components ----------------------------------------------------

    bool getTransform(EntityId id, Transform& out) const;
    void setTransform(EntityId id, const Transform& t);

    bool getSprite(EntityId id, SpriteComponent& out) const;
    void setSprite(EntityId id, const SpriteComponent& s);

    bool getSpriteRenderer(EntityId id, SpriteRendererComponent& out) const;
    void setSpriteRenderer(EntityId id,
                           const std::optional<SpriteRendererComponent>& renderer);

    bool getSpriteAnimator(EntityId id, SpriteAnimatorComponent& out) const;
    void setSpriteAnimator(EntityId id,
                           const std::optional<SpriteAnimatorComponent>& animator);

    bool getPhysics(EntityId id, PhysicsComponent& out) const;
    void setPhysics(EntityId id, const PhysicsComponent& p);

    bool getCollisionBody(EntityId id, CollisionBodyComponent& out) const;
    void setCollisionBody(EntityId id, const std::optional<CollisionBodyComponent>& c);

    bool getPlatformer(EntityId id, PlatformerControllerComponent& out) const;
    void setPlatformer(EntityId id,
                       const std::optional<PlatformerControllerComponent>& p);

    bool getTopDown(EntityId id, TopDownControllerComponent& out) const;
    void setTopDown(EntityId id,
                    const std::optional<TopDownControllerComponent>& t);

    bool getLinearMover(EntityId id, LinearMoverComponent& out) const;
    void setLinearMover(EntityId id,
                        const std::optional<LinearMoverComponent>& m);

    bool getCameraTarget(EntityId id, CameraTargetComponent& out) const;
    void setCameraTarget(EntityId id,
                         const std::optional<CameraTargetComponent>& c);

    bool getMagneticItem(EntityId id, MagneticItemComponent& out) const;
    void setMagneticItem(EntityId id,
                         const std::optional<MagneticItemComponent>& m);

    bool getHordeMember(EntityId id, HordeMemberComponent& out) const;
    void setHordeMember(EntityId id,
                        const std::optional<HordeMemberComponent>& h);

    bool getAutoDestroy(EntityId id, AutoDestroyComponent& out) const;
    void setAutoDestroy(EntityId id,
                        const std::optional<AutoDestroyComponent>& ad);

    bool getText(EntityId id, TextComponent& out) const;
    void setText(EntityId id, const std::optional<TextComponent>& t);

    bool getGauge(EntityId id, GaugeComponent& out) const;
    void setGauge(EntityId id, const std::optional<GaugeComponent>& g);

    bool getDialog(EntityId id, DialogComponent& out) const;
    void setDialog(EntityId id, const std::optional<DialogComponent>& d);

    // ---- Physics handle ------------------------------------------------

    uint32_t physicsHandle(EntityId id) const;
    void     setPhysicsHandle(EntityId id, uint32_t handle);

    // ---- Signal-driven external resource cleanup -----------------------
    //
    // The registry owns physics body teardown via on_destroy<PhysicsHandleComp>.
    // When an entity is destroyed (entity-wide destroy or registry.clear),
    // the signal fires with the still-set handle, and the registry calls
    // physics->destroyBody(handle) automatically. This closes the leak
    // path where `replaceProject` / shutdown cleared entities without
    // calling teardownPhysicsBody first.
    //
    // The pointer is owned by the application; the registry never deletes
    // it. Pass nullptr before tearing down the physics module to disable
    // the signal callback. Named "attach*" rather than setPhysics() to
    // avoid overload ambiguity with setPhysics(EntityId, PhysicsComponent).
    void attachPhysicsModule(Physics* physics);

    // ---- Lifecycle events ----------------------------------------------
    //
    // Filled by on_construct<Identity> (Spawned) and on_destroy<Identity>
    // (Destroyed). The gateway drains once per frame and feeds Lua-side
    // lifecycle.onSpawn/onDestroy handlers. See LifecycleEvent in
    // core/types.h.
    void drainLifecycleEvents(std::vector<LifecycleEvent>& out);

    // ---- System visitors (entt-backed, deterministic order) ------------
    //
    // Each visitor walks insertion-order ids, filters via entt::registry
    // try_get<...> for the required components, and invokes the callback.
    // EnTT does the typed component access; the gateway owns the ordering
    // policy (Lua reproducibility — see docs/ECS_IMPLEMENTATION_GUIDE.md).
    //
    // "ActiveScene" variants additionally require SceneActiveTag presence,
    // i.e. the entity is currently live in the active scene.

    using ActiveRenderableFn = std::function<void(
        EntityId, const Transform&, const SpriteComponent&)>;
    void forEachActiveRenderable(const ActiveRenderableFn& fn) const;

    using ActiveHiddenInGameFn = std::function<void(
        EntityId, const Transform&, const PhysicsComponent&)>;
    void forEachActiveHiddenInGame(const ActiveHiddenInGameFn& fn) const;

    using ActivePhysicsBodyFn = std::function<void(
        EntityId, uint32_t handle, Transform&)>;
    void forEachActivePhysicsBody(const ActivePhysicsBodyFn& fn);

    using ActiveCollisionBodyFn = std::function<void(
        EntityId, const Transform&, const CollisionBodyComponent&)>;
    void forEachActiveCollisionBody(const ActiveCollisionBodyFn& fn) const;

    using ActivePlatformerFn = std::function<void(
        EntityId, const PlatformerControllerComponent&)>;
    void forEachActivePlatformer(const ActivePlatformerFn& fn) const;

    using ActiveTopDownFn = std::function<void(
        EntityId, const TopDownControllerComponent&)>;
    void forEachActiveTopDown(const ActiveTopDownFn& fn) const;

    using ActiveLinearMoverFn = std::function<void(
        EntityId, const LinearMoverComponent&)>;
    void forEachActiveLinearMover(const ActiveLinearMoverFn& fn) const;

    using ActiveCameraTargetFn = std::function<void(
        EntityId, const CameraTargetComponent&)>;
    void forEachActiveCameraTarget(const ActiveCameraTargetFn& fn) const;

    using ActiveMagneticItemFn = std::function<void(
        EntityId, const MagneticItemComponent&)>;
    void forEachActiveMagneticItem(const ActiveMagneticItemFn& fn) const;

    using ActiveHordeMemberFn = std::function<void(
        EntityId, const HordeMemberComponent&)>;
    void forEachActiveHordeMember(const ActiveHordeMemberFn& fn) const;

    using ActiveAutoDestroyFn = std::function<void(
        EntityId, AutoDestroyComponent&)>;
    void forEachActiveAutoDestroy(const ActiveAutoDestroyFn& fn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ArtCade::Modules
