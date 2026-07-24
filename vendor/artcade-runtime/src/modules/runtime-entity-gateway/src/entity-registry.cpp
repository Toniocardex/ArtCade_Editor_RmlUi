// entity-registry.cpp — EnTT-backed implementation of EntityRegistry.
//
// Public API is dictated by entity-registry.h (which the gateway includes).
// All EnTT-specific types live here; the gateway only sees the EntityId
// uint32 surface.
//
// Component layout inside `entt::registry`:
//   - Transform, SpriteComponent, PhysicsComponent : always present once a
//     record is touched (mirrors the previous "default record on first
//     write" semantics; equivalent to value-initialized fields in the old
//     Record struct).
//   - PlatformerControllerComponent, AutoDestroyComponent:
//     opt-in components — emplaced when set with a non-empty optional,
//     removed when set with std::nullopt.
//   - SceneActiveTag : empty tag, presence == active in current scene.
//   - PhysicsHandle  : small uint32 wrapper, separate from PhysicsComponent
//     because the handle changes for body lifetime independently of the
//     authored physics data. on_destroy<PhysicsHandleComp> frees the physics
//     body automatically when the entity is destroyed or registry.clear()
//     runs (signal-driven cleanup; see Impl::onPhysicsHandleDestroyed).
//   - Identity       : className + tags metadata, deliberately NOT default-
//                      emplaced (only added by setIdentity) so that
//                      on_construct/on_destroy<Identity> fire exactly once
//                      per spawn/destroy and drive both manual indices
//                      (classIndex / tagIndex) and lifecycle events.

#include "entity-registry.h"
#include "../../physics/include/physics.h"

#include <entt/entt.hpp>

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace ArtCade::Modules {

namespace {

struct PhysicsHandleComp { uint32_t value = 0; };
struct SceneActiveTag    {};
struct VisibleInGameComp { bool value = true; };
struct Identity {
    std::string              className;
    std::vector<std::string> tags;
};

void eraseId(std::vector<EntityId>& ids, EntityId id) {
    ids.erase(std::remove(ids.begin(), ids.end(), id), ids.end());
}

} // namespace

struct EntityRegistry::Impl {
    entt::registry reg;

    // EntityId (project-stable uint32) → entt::entity (entt's compact id).
    std::unordered_map<EntityId, entt::entity> ids;
    // Reverse lookup used inside signal callbacks where we only have an
    // entt::entity (passed by EnTT) and need the project-stable EntityId
    // to update the manual indices and emit lifecycle events.
    std::unordered_map<entt::entity, EntityId> reverseIds;

    // Insertion-order list of *all* ids. Mirrors the order in which entities
    // were allocated/touched, with erase O(N) — the ArtCade workload is
    // hundreds/few-thousand entities so the linear scan is cheaper than
    // dragging a second index. Lua determinism requires this stable order:
    // entt views and the `ids` hashmap give no portable iteration order.
    std::vector<EntityId> insertionOrder;

    // Insertion-order indexes for deterministic iteration. EnTT views do
    // not guarantee a stable ordering across runs, so we maintain these
    // through on_construct/on_destroy<Identity> signals (see below).
    std::unordered_map<std::string, std::vector<EntityId>> classIndex;
    std::unordered_map<std::string, std::vector<EntityId>> tagIndex;

    // Lifecycle events queued by Identity signals; drained by the gateway
    // once per frame.
    std::vector<LifecycleEvent> lifecycleQueue;

    // External resources reachable from signal callbacks. nullptr disables
    // the corresponding signal-driven cleanup (used during shutdown to
    // avoid touching modules that were already torn down).
    Physics* physics = nullptr;

    EntityId nextId = 1;

    entt::entity toEntt(EntityId id) const {
        auto it = ids.find(id);
        return (it != ids.end()) ? it->second : entt::null;
    }

    EntityId toEntityId(entt::entity e) const {
        auto it = reverseIds.find(e);
        return (it != reverseIds.end()) ? it->second : EntityId{0};
    }

    entt::entity ensure(EntityId id) {
        auto it = ids.find(id);
        if (it != ids.end()) return it->second;
        const entt::entity e = reg.create();
        ids.emplace(id, e);
        reverseIds.emplace(e, id);
        insertionOrder.push_back(id);
        // Default-initialize the "always present" components, matching
        // the previous "value-initialized record fields" contract.
        // Identity is *not* default-emplaced: it's added by setIdentity
        // so that on_construct<Identity> fires exactly once per entity
        // with the real className/tags (and emits the Spawned event).
        reg.emplace<Transform>(e);
        reg.emplace<SpriteComponent>(e);
        reg.emplace<PhysicsComponent>(e);
        reg.emplace<PhysicsHandleComp>(e);
        reg.emplace<VisibleInGameComp>(e);
        return e;
    }

    // ---- Signal handlers --------------------------------------------------

    void onIdentityConstructed(entt::registry& r, entt::entity e) {
        const EntityId id = toEntityId(e);
        if (id == 0) return;
        const auto& ident = r.get<Identity>(e);
        if (!ident.className.empty())
            classIndex[ident.className].push_back(id);
        for (const std::string& tag : ident.tags)
            tagIndex[tag].push_back(id);
        lifecycleQueue.push_back(LifecycleEvent{
            LifecycleEvent::Kind::Spawned, id, ident.className, ident.tags });
    }

    void onIdentityDestroyed(entt::registry& r, entt::entity e) {
        const EntityId id = toEntityId(e);
        if (id == 0) return;
        const auto& ident = r.get<Identity>(e);
        if (!ident.className.empty()) {
            auto cit = classIndex.find(ident.className);
            if (cit != classIndex.end()) eraseId(cit->second, id);
        }
        for (const std::string& tag : ident.tags) {
            auto tit = tagIndex.find(tag);
            if (tit != tagIndex.end()) eraseId(tit->second, id);
        }
        lifecycleQueue.push_back(LifecycleEvent{
            LifecycleEvent::Kind::Destroyed, id, ident.className, ident.tags });
    }

    void onPhysicsHandleDestroyed(entt::registry& r, entt::entity e) {
        if (!physics) return;
        const auto* h = r.try_get<PhysicsHandleComp>(e);
        if (!h || h->value == 0) return;
        physics->destroyBody(h->value);
    }
};

EntityRegistry::EntityRegistry() : impl_(std::make_unique<Impl>()) {
    // Wire the signals once. Lifetimes: Impl owns the registry and outlives
    // the connections; the connect-by-member-pointer pattern below stores
    // a pointer to *impl_ which is stable for the registry's lifetime.
    impl_->reg.on_construct<Identity>()
        .connect<&Impl::onIdentityConstructed>(*impl_);
    impl_->reg.on_destroy<Identity>()
        .connect<&Impl::onIdentityDestroyed>(*impl_);
    impl_->reg.on_destroy<PhysicsHandleComp>()
        .connect<&Impl::onPhysicsHandleDestroyed>(*impl_);
}

EntityRegistry::~EntityRegistry() = default;

// ---- Records ---------------------------------------------------------------

EntityId EntityRegistry::allocate(EntityId hint) {
    EntityId id;
    if (hint != 0 && impl_->ids.find(hint) == impl_->ids.end()) {
        id = hint;
    } else {
        while (impl_->ids.find(impl_->nextId) != impl_->ids.end())
            ++impl_->nextId;
        id = impl_->nextId++;
    }
    if (id >= impl_->nextId) impl_->nextId = id + 1;
    impl_->ensure(id);
    return id;
}

bool EntityRegistry::touch(EntityId id) {
    if (impl_->ids.find(id) != impl_->ids.end()) return false;
    impl_->ensure(id);
    return true;
}

void EntityRegistry::erase(EntityId id) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    // reg.destroy fires on_destroy signals for every component the entity
    // holds; the Identity and PhysicsHandle handlers do their own cleanup
    // (indices, lifecycle event, physics body). After this call the slot is
    // gone from the EnTT registry — we only need to mop up the manual
    // EntityId→entt::entity bookkeeping.
    impl_->reg.destroy(e);
    impl_->ids.erase(id);
    impl_->reverseIds.erase(e);
    eraseId(impl_->insertionOrder, id);
}

bool EntityRegistry::contains(EntityId id) const {
    return impl_->ids.find(id) != impl_->ids.end();
}

void EntityRegistry::clear() {
    // reg.clear() fires on_destroy for every component of every entity —
    // class/tag indexes drain themselves and physics bodies are freed via
    // the PhysicsHandleComp signal. We just reset the manual bookkeeping.
    impl_->reg.clear();
    impl_->ids.clear();
    impl_->reverseIds.clear();
    impl_->insertionOrder.clear();
    impl_->classIndex.clear();
    impl_->tagIndex.clear();
    // Discard lifecycle events emitted during clear(): on shutdown/project
    // reload Lua handlers either don't exist yet or refer to a stale state
    // and would receive ghost Destroyed events for entities they never saw
    // spawn. Callers that need to observe a full teardown should drain
    // *before* invoking clear().
    impl_->lifecycleQueue.clear();
    impl_->nextId = 1;
}

void EntityRegistry::attachPhysicsModule(Physics* physics) {
    impl_->physics = physics;
}

void EntityRegistry::drainLifecycleEvents(std::vector<LifecycleEvent>& out) {
    if (impl_->lifecycleQueue.empty()) return;
    out.insert(out.end(),
               std::make_move_iterator(impl_->lifecycleQueue.begin()),
               std::make_move_iterator(impl_->lifecycleQueue.end()));
    impl_->lifecycleQueue.clear();
}

std::vector<EntityId> EntityRegistry::allIds() const {
    return impl_->insertionOrder;
}

// ---- Identity + indexes ---------------------------------------------------

void EntityRegistry::setIdentity(EntityId id,
                                 std::string className,
                                 std::vector<std::string> tags) {
    const entt::entity e = impl_->ensure(id);
    // Idempotency guard: setting the same identity twice (e.g. during a
    // re-bind / hot reload that walks the project defs again) must not
    // fire spurious Destroyed+Spawned lifecycle events. If the existing
    // Identity matches exactly, no-op.
    if (const auto* existing = impl_->reg.try_get<Identity>(e)) {
        if (existing->className == className && existing->tags == tags)
            return;
    }
    // Remove first so on_destroy<Identity> fires with the *old* className
    // and tags — that's what the signal handler reads to scrub the manual
    // indices and emit the Destroyed lifecycle event (no-op if Identity
    // wasn't present yet). The subsequent emplace fires on_construct
    // with the new data, which repopulates the indices and emits Spawned.
    // NOTE: a true *rename* still produces Destroyed→Spawned, which is
    // semantically a re-identification rather than a respawn. Callers
    // that care should expose their own "rename" pathway; today the
    // gateway only calls setIdentity at entity creation so this is
    // dead-code in practice.
    impl_->reg.remove<Identity>(e);
    impl_->reg.emplace<Identity>(e,
        Identity{ std::move(className), std::move(tags) });
}

const std::string& EntityRegistry::className(EntityId id) const {
    static const std::string empty;
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return empty;
    if (const auto* ident = impl_->reg.try_get<Identity>(e))
        return ident->className;
    return empty;
}

const std::vector<std::string>& EntityRegistry::tags(EntityId id) const {
    static const std::vector<std::string> empty;
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return empty;
    if (const auto* ident = impl_->reg.try_get<Identity>(e))
        return ident->tags;
    return empty;
}

const std::vector<EntityId>&
EntityRegistry::idsByClass(const std::string& className) const {
    static const std::vector<EntityId> kEmpty;
    auto it = impl_->classIndex.find(className);
    if (it == impl_->classIndex.end()) return kEmpty;
    return it->second;
}

const std::vector<EntityId>&
EntityRegistry::idsByTag(const std::string& tag) const {
    static const std::vector<EntityId> kEmpty;
    auto it = impl_->tagIndex.find(tag);
    if (it == impl_->tagIndex.end()) return kEmpty;
    return it->second;
}

// ---- Scene activation -----------------------------------------------------

bool EntityRegistry::sceneActive(EntityId id) const {
    const entt::entity e = impl_->toEntt(id);
    return e != entt::null && impl_->reg.all_of<SceneActiveTag>(e);
}

void EntityRegistry::setSceneActive(EntityId id, bool active) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (active) impl_->reg.emplace_or_replace<SceneActiveTag>(e);
    else        impl_->reg.remove<SceneActiveTag>(e);
}

bool EntityRegistry::visibleInGame(EntityId id) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return true;
    if (const auto* v = impl_->reg.try_get<VisibleInGameComp>(e))
        return v->value;
    return true;
}

void EntityRegistry::setVisibleInGame(EntityId id, bool visible) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    impl_->reg.emplace_or_replace<VisibleInGameComp>(e, VisibleInGameComp{ visible });
}

// ---- Components -----------------------------------------------------------

bool EntityRegistry::getTransform(EntityId id, Transform& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<Transform>(e)) { out = *c; return true; }
    return false;
}

void EntityRegistry::setTransform(EntityId id, const Transform& t) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    impl_->reg.emplace_or_replace<Transform>(e, t);
}

bool EntityRegistry::getSprite(EntityId id, SpriteComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<SpriteComponent>(e)) { out = *c; return true; }
    return false;
}

void EntityRegistry::setSprite(EntityId id, const SpriteComponent& s) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    impl_->reg.emplace_or_replace<SpriteComponent>(e, s);
}

bool EntityRegistry::getPhysics(EntityId id, PhysicsComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<PhysicsComponent>(e)) {
        out = *c;
        if (const auto* h = impl_->reg.try_get<PhysicsHandleComp>(e))
            out.physicsHandle = h->value;
        return true;
    }
    return false;
}

bool EntityRegistry::getSpriteRenderer(EntityId id, SpriteRendererComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    const auto* value = impl_->reg.try_get<SpriteRendererComponent>(e);
    if (!value) return false;
    out = *value;
    return true;
}

void EntityRegistry::setSpriteRenderer(
    EntityId id, const std::optional<SpriteRendererComponent>& renderer) {
    const entt::entity e = impl_->ensure(id);
    if (renderer)
        impl_->reg.emplace_or_replace<SpriteRendererComponent>(e, *renderer);
    else
        impl_->reg.remove<SpriteRendererComponent>(e);
}

bool EntityRegistry::getSpriteAnimator(EntityId id, SpriteAnimatorComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    const auto* value = impl_->reg.try_get<SpriteAnimatorComponent>(e);
    if (!value) return false;
    out = *value;
    return true;
}

void EntityRegistry::setSpriteAnimator(
    EntityId id, const std::optional<SpriteAnimatorComponent>& animator) {
    const entt::entity e = impl_->ensure(id);
    if (animator)
        impl_->reg.emplace_or_replace<SpriteAnimatorComponent>(e, *animator);
    else
        impl_->reg.remove<SpriteAnimatorComponent>(e);
}

void EntityRegistry::setPhysics(EntityId id, const PhysicsComponent& p) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    impl_->reg.emplace_or_replace<PhysicsComponent>(e, p);
    impl_->reg.emplace_or_replace<PhysicsHandleComp>(e, PhysicsHandleComp{ p.physicsHandle });
}

bool EntityRegistry::getPlatformer(EntityId id,
                                   PlatformerControllerComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<PlatformerControllerComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setPlatformer(
    EntityId id, const std::optional<PlatformerControllerComponent>& p) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (p) impl_->reg.emplace_or_replace<PlatformerControllerComponent>(e, *p);
    else   impl_->reg.remove<PlatformerControllerComponent>(e);
}

bool EntityRegistry::getTopDown(EntityId id,
                                TopDownControllerComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<TopDownControllerComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setTopDown(
    EntityId id, const std::optional<TopDownControllerComponent>& t) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (t) impl_->reg.emplace_or_replace<TopDownControllerComponent>(e, *t);
    else   impl_->reg.remove<TopDownControllerComponent>(e);
}

bool EntityRegistry::getLinearMover(EntityId id,
                                    LinearMoverComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<LinearMoverComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setLinearMover(
    EntityId id, const std::optional<LinearMoverComponent>& m) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (m) impl_->reg.emplace_or_replace<LinearMoverComponent>(e, *m);
    else   impl_->reg.remove<LinearMoverComponent>(e);
}

bool EntityRegistry::getCameraTarget(EntityId id,
                                     CameraTargetComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<CameraTargetComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setCameraTarget(
    EntityId id, const std::optional<CameraTargetComponent>& c) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (c) impl_->reg.emplace_or_replace<CameraTargetComponent>(e, *c);
    else   impl_->reg.remove<CameraTargetComponent>(e);
}

bool EntityRegistry::getMagneticItem(EntityId id,
                                     MagneticItemComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<MagneticItemComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setMagneticItem(
    EntityId id, const std::optional<MagneticItemComponent>& m) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (m) impl_->reg.emplace_or_replace<MagneticItemComponent>(e, *m);
    else   impl_->reg.remove<MagneticItemComponent>(e);
}

bool EntityRegistry::getHordeMember(EntityId id,
                                    HordeMemberComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<HordeMemberComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setHordeMember(
    EntityId id, const std::optional<HordeMemberComponent>& h) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (h) impl_->reg.emplace_or_replace<HordeMemberComponent>(e, *h);
    else   impl_->reg.remove<HordeMemberComponent>(e);
}

bool EntityRegistry::getAutoDestroy(EntityId id,
                                    AutoDestroyComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<AutoDestroyComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setAutoDestroy(
    EntityId id, const std::optional<AutoDestroyComponent>& ad) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (ad) impl_->reg.emplace_or_replace<AutoDestroyComponent>(e, *ad);
    else    impl_->reg.remove<AutoDestroyComponent>(e);
}

bool EntityRegistry::getText(EntityId id, TextComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<TextComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setText(EntityId id,
                             const std::optional<TextComponent>& t) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (t) impl_->reg.emplace_or_replace<TextComponent>(e, *t);
    else   impl_->reg.remove<TextComponent>(e);
}

bool EntityRegistry::getGauge(EntityId id, GaugeComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<GaugeComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setGauge(EntityId id,
                              const std::optional<GaugeComponent>& g) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (g) impl_->reg.emplace_or_replace<GaugeComponent>(e, *g);
    else   impl_->reg.remove<GaugeComponent>(e);
}

bool EntityRegistry::getCollisionBody(EntityId id, CollisionBodyComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<CollisionBodyComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setCollisionBody(
    EntityId id,
    const std::optional<CollisionBodyComponent>& c) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (c) impl_->reg.emplace_or_replace<CollisionBodyComponent>(e, *c);
    else   impl_->reg.remove<CollisionBodyComponent>(e);
}

bool EntityRegistry::getDialog(EntityId id, DialogComponent& out) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return false;
    if (const auto* c = impl_->reg.try_get<DialogComponent>(e)) {
        out = *c;
        return true;
    }
    return false;
}

void EntityRegistry::setDialog(EntityId id,
                               const std::optional<DialogComponent>& d) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    if (d) impl_->reg.emplace_or_replace<DialogComponent>(e, *d);
    else   impl_->reg.remove<DialogComponent>(e);
}

// ---- Physics handle -------------------------------------------------------

uint32_t EntityRegistry::physicsHandle(EntityId id) const {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return 0u;
    if (const auto* h = impl_->reg.try_get<PhysicsHandleComp>(e))
        return h->value;
    return 0u;
}

void EntityRegistry::setPhysicsHandle(EntityId id, uint32_t handle) {
    const entt::entity e = impl_->toEntt(id);
    if (e == entt::null) return;
    impl_->reg.emplace_or_replace<PhysicsHandleComp>(e, PhysicsHandleComp{ handle });
    if (auto* p = impl_->reg.try_get<PhysicsComponent>(e))
        p->physicsHandle = handle;
}

// ---- System visitors -------------------------------------------------------
//
// Common loop pattern:
//   capture insertionOrder.size() ONCE up front (re-entry safety: callback
//     might queue spawns that push into insertionOrder; those are visited
//     next frame, never re-entered in this pass).
//   for i in [0, n):
//     id = insertionOrder[i];
//     e  = toEntt(id);  if (e == null) continue;  // destroyed mid-iter
//     if (!reg.all_of<SceneActiveTag>(e)) continue;
//     try_get<...> required components → invoke callback if all present.
//
// Insertion order keeps Lua-observable outcomes (sensor edges, queued
// destroys, gameplay velocities) reproducible across runs. EnTT's
// archetype storage gives O(1) typed access inside the loop.

void EntityRegistry::forEachActiveRenderable(
    const ActiveRenderableFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* t = reg.try_get<Transform>(e);
        const auto* s = reg.try_get<SpriteComponent>(e);
        if (!t || !s) continue;
        fn(id, *t, *s);
    }
}

void EntityRegistry::forEachActiveHiddenInGame(
    const ActiveHiddenInGameFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* v = reg.try_get<VisibleInGameComp>(e);
        if (!v || v->value) continue;
        const auto* t = reg.try_get<Transform>(e);
        if (!t) continue;
        const auto* p = reg.try_get<PhysicsComponent>(e);
        PhysicsComponent fallback{};
        fn(id, *t, p ? *p : fallback);
    }
}

void EntityRegistry::forEachActivePhysicsBody(
    const ActivePhysicsBodyFn& fn)
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* h = reg.try_get<PhysicsHandleComp>(e);
        if (!h || h->value == 0) continue;
        auto* t = reg.try_get<Transform>(e);
        if (!t) continue;
        fn(id, h->value, *t);
    }
}

void EntityRegistry::forEachActiveCollisionBody(
    const ActiveCollisionBodyFn& fn) const
{
    for (EntityId id : impl_->insertionOrder) {
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null || !impl_->reg.all_of<SceneActiveTag>(e))
            continue;
        const auto* transform = impl_->reg.try_get<Transform>(e);
        const auto* body = impl_->reg.try_get<CollisionBodyComponent>(e);
        if (transform && body)
            fn(id, *transform, *body);
    }
}

void EntityRegistry::forEachActivePlatformer(
    const ActivePlatformerFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* p = reg.try_get<PlatformerControllerComponent>(e);
        if (!p) continue;
        fn(id, *p);
    }
}

void EntityRegistry::forEachActiveTopDown(
    const ActiveTopDownFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* t = reg.try_get<TopDownControllerComponent>(e);
        if (!t) continue;
        fn(id, *t);
    }
}

void EntityRegistry::forEachActiveLinearMover(
    const ActiveLinearMoverFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* m = reg.try_get<LinearMoverComponent>(e);
        if (!m) continue;
        fn(id, *m);
    }
}

void EntityRegistry::forEachActiveCameraTarget(
    const ActiveCameraTargetFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* c = reg.try_get<CameraTargetComponent>(e);
        if (!c) continue;
        fn(id, *c);
    }
}

void EntityRegistry::forEachActiveMagneticItem(
    const ActiveMagneticItemFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* m = reg.try_get<MagneticItemComponent>(e);
        if (!m) continue;
        fn(id, *m);
    }
}

void EntityRegistry::forEachActiveHordeMember(
    const ActiveHordeMemberFn& fn) const
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        const auto* h = reg.try_get<HordeMemberComponent>(e);
        if (!h) continue;
        fn(id, *h);
    }
}

void EntityRegistry::forEachActiveAutoDestroy(
    const ActiveAutoDestroyFn& fn)
{
    auto& reg = impl_->reg;
    const size_t n = impl_->insertionOrder.size();
    for (size_t i = 0; i < n; ++i) {
        const EntityId id = impl_->insertionOrder[i];
        const entt::entity e = impl_->toEntt(id);
        if (e == entt::null) continue;
        if (!reg.all_of<SceneActiveTag>(e)) continue;
        auto* a = reg.try_get<AutoDestroyComponent>(e);
        if (!a) continue;
        fn(id, *a);
    }
}

} // namespace ArtCade::Modules
