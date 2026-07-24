// physics.cpp — Custom iterative physics (Facade: physics.h unchanged).
#include "../include/physics.h"
#include <collision_math.h>

#define RAYMATH_STATIC_INLINE
#include "raymath.h"

#include <unordered_map>
#include <vector>
#include <cmath>
#include <cstdint>

namespace ArtCade::Modules {

namespace {

using namespace PhysicsMath;

constexpr int   kResolvePasses     = 4;
constexpr size_t kBroadphaseThreshold = 64;
constexpr float kBroadphaseCellSize   = 64.f;
constexpr float kCcdMoveFraction      = 0.5f; // vs min collider half-extent

inline Vector2 toRay(const Vec2& v) { return { v.x, v.y }; }
inline Vec2    fromRay(Vector2 v)  { return { v.x, v.y }; }

inline void zeroVelocityIntoSurface(Vec2& vel, const Vec2& correction) {
    if (std::abs(correction.x) > 1e-6f) {
        if (correction.x > 0.f && vel.x < 0.f) vel.x = 0.f;
        if (correction.x < 0.f && vel.x > 0.f) vel.x = 0.f;
    }
    if (std::abs(correction.y) > 1e-6f) {
        if (correction.y > 0.f && vel.y < 0.f) vel.y = 0.f;
        if (correction.y < 0.f && vel.y > 0.f) vel.y = 0.f;
    }
}

inline int64_t broadphaseCellKey(int cx, int cy) {
    return (static_cast<int64_t>(cx) << 32)
         | (static_cast<uint32_t>(cy) & 0xffffffffu);
}

} // namespace

struct Physics::Impl {
    Vec2 worldGravity{ 0.f, 10.f };

    struct BodyEntry {
        EntityId        entityId   = INVALID_ENTITY;
        BodyType        bodyType   = BodyType::Dynamic;
        Collider        collider;
        Vec2            position;
        Vec2            velocity;
        Vec2            accumulatedForce;
        float           gravityScale = 1.f;
        bool            active       = true;
    };

    std::unordered_map<uint32_t, BodyEntry> bodies;
    uint32_t nextHandle = 1;

    static float minColliderHalfExtent(const BodyEntry& body) {
        if (body.collider.shape == ColliderShape::Circle)
            return std::max(0.5f, body.collider.size.x);
        return std::max(0.5f,
            std::min(body.collider.size.x, body.collider.size.y) * 0.5f);
    }

    static ShapeInstance mainShape(const BodyEntry& body) {
        ShapeInstance s;
        s.shape    = body.collider.shape;
        s.position = body.position;
        s.offset   = body.collider.offset;
        s.size     = body.collider.size;
        return s;
    }

    static bool shapesOverlapAny(const BodyEntry& a, const BodyEntry& b) {
        return shapesOverlap(mainShape(a), mainShape(b));
    }

    static bool shouldResolvePair(const BodyEntry& a, const BodyEntry& b) {
        // Platformer controllers are Kinematic; they must still collide with
        // Static geometry. Skip only Static-Static and Kinematic-Kinematic.
        if (a.bodyType == BodyType::Static && b.bodyType == BodyType::Static)
            return false;
        if (a.bodyType == BodyType::Kinematic && b.bodyType == BodyType::Kinematic)
            return false;
        return true;
    }

    static bool isMovable(BodyType type) {
        return type == BodyType::Dynamic || type == BodyType::Kinematic;
    }

    static void resolvePair(BodyEntry& a, BodyEntry& b) {
        if (!aabbOverlap(shapeWorldAabb(mainShape(a)), shapeWorldAabb(mainShape(b))))
            return;

        const bool aMov = isMovable(a.bodyType);
        const bool bMov = isMovable(b.bodyType);

        if (aMov && bMov) {
            Aabb boxA = shapeWorldAabb(mainShape(a));
            Aabb boxB = shapeWorldAabb(mainShape(b));
            Vec2 correction{};
            if (!resolveAabbSeparation(boxA, boxB, correction))
                return;
            const Vec2 half = { correction.x * 0.5f, correction.y * 0.5f };
            a.position.x += half.x;
            a.position.y += half.y;
            b.position.x -= half.x;
            b.position.y -= half.y;
            zeroVelocityIntoSurface(a.velocity, correction);
            zeroVelocityIntoSurface(b.velocity, { -correction.x, -correction.y });
            return;
        }

        BodyEntry* movable = aMov ? &a : (bMov ? &b : nullptr);
        BodyEntry* fixed   = aMov ? &b : &a;
        if (!movable)
            return;

        Aabb movableBox = shapeWorldAabb(mainShape(*movable));
        Aabb fixedBox   = shapeWorldAabb(mainShape(*fixed));
        Vec2 correction{};
        if (!resolveAabbSeparation(movableBox, fixedBox, correction))
            return;

        movable->position.x += correction.x;
        movable->position.y += correction.y;
        zeroVelocityIntoSurface(movable->velocity, correction);
    }

    static void resolvePairHandles(std::unordered_map<uint32_t, BodyEntry>& bodies,
                                   uint32_t ha, uint32_t hb) {
        auto itA = bodies.find(ha);
        auto itB = bodies.find(hb);
        if (itA == bodies.end() || itB == bodies.end())
            return;
        if (!shouldResolvePair(itA->second, itB->second))
            return;
        resolvePair(itA->second, itB->second);
    }

    void resolveCollisionsBroadphase(const std::vector<uint32_t>& handles) {
        std::unordered_map<int64_t, std::vector<uint32_t>> cells;
        cells.reserve(handles.size() * 2);

        for (uint32_t h : handles) {
            const Aabb box = shapeWorldAabb(mainShape(bodies.at(h)));
            const int c0 = static_cast<int>(std::floor(box.minX / kBroadphaseCellSize));
            const int c1 = static_cast<int>(std::floor(box.maxX / kBroadphaseCellSize));
            const int r0 = static_cast<int>(std::floor(box.minY / kBroadphaseCellSize));
            const int r1 = static_cast<int>(std::floor(box.maxY / kBroadphaseCellSize));
            for (int r = r0; r <= r1; ++r) {
                for (int c = c0; c <= c1; ++c)
                    cells[broadphaseCellKey(c, r)].push_back(h);
            }
        }

        std::unordered_map<uint64_t, bool> seen;
        seen.reserve(handles.size() * 4);
        for (auto& [key, bucket] : cells) {
            (void)key;
            for (size_t i = 0; i < bucket.size(); ++i) {
                for (size_t j = i + 1; j < bucket.size(); ++j) {
                    uint32_t ha = bucket[i];
                    uint32_t hb = bucket[j];
                    if (ha > hb) std::swap(ha, hb);
                    const uint64_t pairKey =
                        (static_cast<uint64_t>(ha) << 32) | static_cast<uint64_t>(hb);
                    if (seen.count(pairKey)) continue;
                    seen[pairKey] = true;
                    resolvePairHandles(bodies, ha, hb);
                }
            }
        }
    }

    void resolveCollisionsLinear(const std::vector<uint32_t>& handles) {
        for (size_t i = 0; i < handles.size(); ++i) {
            for (size_t j = i + 1; j < handles.size(); ++j)
                resolvePairHandles(bodies, handles[i], handles[j]);
        }
    }

    void resolveCollisions() {
        std::vector<uint32_t> handles;
        handles.reserve(bodies.size());
        for (const auto& [handle, body] : bodies) {
            if (body.active)
                handles.push_back(handle);
        }
        if (handles.size() < 2)
            return;

        for (int pass = 0; pass < kResolvePasses; ++pass) {
            if (handles.size() > kBroadphaseThreshold)
                resolveCollisionsBroadphase(handles);
            else
                resolveCollisionsLinear(handles);
        }
    }

    void integrateDynamicCCD(BodyEntry& body, float subDt) {
        Vector2 vel = toRay(body.velocity);
        const Vector2 gravityStep = Vector2Scale(
            toRay(worldGravity), body.gravityScale * subDt);
        const Vector2 forceStep = Vector2Scale(toRay(body.accumulatedForce), subDt);
        vel = Vector2Add(Vector2Add(vel, gravityStep), forceStep);
        body.velocity = fromRay(vel);

        const Vec2 delta{ body.velocity.x * subDt, body.velocity.y * subDt };
        const float moveLen = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        const float threshold = Impl::minColliderHalfExtent(body) * kCcdMoveFraction;

        if (moveLen > threshold && moveLen > 1e-6f) {
            const Vec2 from = body.position;
            SweepHit best;
            const Aabb movingBox = shapeWorldAabb(mainShape(body));

            for (const auto& [handle, other] : bodies) {
                (void)handle;
                if (!other.active || other.bodyType == BodyType::Dynamic)
                    continue;
                const SweepHit hit =
                    sweepAabb(movingBox, delta, shapeWorldAabb(mainShape(other)));
                if (!hit.hit || hit.fraction >= best.fraction)
                    continue;
                best = hit;
            }

            if (best.hit) {
                const float t = std::max(0.f, best.fraction - 1e-4f);
                body.position.x = from.x + delta.x * t;
                body.position.y = from.y + delta.y * t;
                if (std::abs(best.normal.x) > 0.f)
                    body.velocity.x = 0.f;
                if (std::abs(best.normal.y) > 0.f)
                    body.velocity.y = 0.f;
                return;
            }
        }

        body.position.x += delta.x;
        body.position.y += delta.y;
    }
};

// ============================================================================
// Lifecycle
// ============================================================================

Physics::Physics()  : impl_(std::make_unique<Impl>()) {}
Physics::~Physics() = default;

bool Physics::init()     { return true; }

void Physics::shutdown() {
    destroyAllBodies();
}

void Physics::destroyAllBodies() {
    impl_->bodies.clear();
    impl_->nextHandle = 1;
}

// ============================================================================
// World config
// ============================================================================

void Physics::setGravity(const Vec2& gravity) {
    impl_->worldGravity = gravity;
}

void Physics::step(float dt, uint32_t substeps) {
    if (impl_->bodies.empty())
        return;

    const uint32_t steps = substeps > 0 ? substeps : 1;
    const float    subDt = dt / static_cast<float>(steps);

    for (uint32_t s = 0; s < steps; ++s) {
        for (auto& [handle, body] : impl_->bodies) {
            (void)handle;
            if (!body.active || body.bodyType != BodyType::Dynamic)
                continue;
            impl_->integrateDynamicCCD(body, subDt);
        }

        impl_->resolveCollisions();
    }

    for (auto& [handle, body] : impl_->bodies) {
        (void)handle;
        body.accumulatedForce = {};
    }
}

bool Physics::hasActiveBodies() const {
    return !impl_->bodies.empty();
}

bool Physics::hasDynamicBodies() const {
    for (const auto& [handle, body] : impl_->bodies) {
        (void)handle;
        if (body.active && body.bodyType == BodyType::Dynamic)
            return true;
    }
    return false;
}

// ============================================================================
// Body lifecycle
// ============================================================================

uint32_t Physics::createBody(EntityId entityId, const PhysicsComponent& comp) {
    Impl::BodyEntry entry;
    entry.entityId     = entityId;
    entry.bodyType     = comp.bodyType;
    entry.collider     = comp.collider;
    entry.position     = {};
    entry.velocity     = {};
    entry.accumulatedForce = {};
    entry.gravityScale = 1.f;
    entry.active       = true;

    const uint32_t handle = impl_->nextHandle++;
    impl_->bodies[handle] = entry;
    return handle;
}

void Physics::destroyBody(uint32_t handle) {
    impl_->bodies.erase(handle);
}

void Physics::setBodyActive(uint32_t handle, bool active) {
    auto it = impl_->bodies.find(handle);
    if (it == impl_->bodies.end()) return;
    it->second.active = active;
}

// ============================================================================
// Velocity / Position
// ============================================================================

void Physics::setLinearVelocity(uint32_t handle, const Vec2& vel) {
    auto it = impl_->bodies.find(handle);
    if (it != impl_->bodies.end())
        it->second.velocity = vel;
}

void Physics::applyImpulse(uint32_t handle, const Vec2& impulse) {
    auto it = impl_->bodies.find(handle);
    if (it == impl_->bodies.end() || it->second.bodyType != BodyType::Dynamic)
        return;
    it->second.velocity.x += impulse.x;
    it->second.velocity.y += impulse.y;
}

void Physics::applyForce(uint32_t handle, const Vec2& force) {
    auto it = impl_->bodies.find(handle);
    if (it == impl_->bodies.end() || it->second.bodyType != BodyType::Dynamic)
        return;
    it->second.accumulatedForce.x += force.x;
    it->second.accumulatedForce.y += force.y;
}

void Physics::setGravityScale(uint32_t handle, float scale) {
    auto it = impl_->bodies.find(handle);
    if (it != impl_->bodies.end())
        it->second.gravityScale = scale;
}

Vec2 Physics::getLinearVelocity(uint32_t handle) const {
    auto it = impl_->bodies.find(handle);
    if (it == impl_->bodies.end()) return {};
    return it->second.velocity;
}

void Physics::setPosition(uint32_t handle, const Vec2& pos) {
    auto it = impl_->bodies.find(handle);
    if (it == impl_->bodies.end()) return;
    it->second.position = pos;
}

Vec2 Physics::getPosition(uint32_t handle) const {
    auto it = impl_->bodies.find(handle);
    if (it == impl_->bodies.end()) return {};
    return it->second.position;
}

// ============================================================================
// Collision queries
// ============================================================================

bool Physics::areOverlapping(uint32_t h1, uint32_t h2) const {
    auto it1 = impl_->bodies.find(h1);
    auto it2 = impl_->bodies.find(h2);
    if (it1 == impl_->bodies.end() || it2 == impl_->bodies.end()) return false;
    if (!it1->second.active || !it2->second.active) return false;
    return Impl::shapesOverlapAny(it1->second, it2->second);
}

Physics::RaycastResult Physics::raycast(const Vec2& from, const Vec2& to) const {
    RaycastResult result;
    RaycastHit    best;

    for (const auto& [handle, body] : impl_->bodies) {
        if (!body.active)
            continue;

        const RaycastHit hit = raycastSegmentVsShape(from, to, Impl::mainShape(body));
        if (!hit.hit || hit.fraction >= best.fraction)
            continue;

        best = hit;
        result.hit      = true;
        result.handle   = handle;
        result.entityId = body.entityId;
        result.point    = hit.point;
    }

    if (!result.hit)
        return result;

    const float dx = result.point.x - from.x;
    const float dy = result.point.y - from.y;
    result.distance = std::sqrt(dx * dx + dy * dy);
    return result;
}

std::vector<uint32_t> Physics::getContactingBodies(const Vec2& point) const {
    std::vector<uint32_t> result;
    for (const auto& [handle, body] : impl_->bodies) {
        if (!body.active) continue;
        if (pointInsideShape(point, Impl::mainShape(body)))
            result.push_back(handle);
    }
    return result;
}

} // namespace ArtCade::Modules
