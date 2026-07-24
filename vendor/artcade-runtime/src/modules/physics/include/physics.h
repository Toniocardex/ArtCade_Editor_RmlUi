#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include <memory>
#include <vector>

namespace ArtCade::Modules {

/**
 * Physics — custom 2D solver (Facade; Fase 12+).
 *
 * Implementation in physics.cpp + artcade-collision (semi-implicit Euler,
 * linear broadphase). External modules see only Vec2 / PhysicsComponent / handle.
 *
 * Screen-space Y-down. Default gravity: {0, +10} (fall toward +Y).
 */
class Physics final : public IModule {
public:
    Physics();
    ~Physics();          // definito in .cpp dove Impl è completo

    bool init()     override;
    void shutdown() override;

    // ---- World config -------------------------------------------------------
    void setGravity(const Vec2& gravity);

    // Fixed-timestep simulation step (substeps interni per stabilità)
    void step(float dt, uint32_t substeps = 2);
    /** True when at least one physics body exists (used for physicsMode auto). */
    bool hasActiveBodies() const;
    /** True when at least one active Dynamic body exists (tilemap body policy). */
    bool hasDynamicBodies() const;

    // ---- Body lifecycle -----------------------------------------------------
    uint32_t createBody(EntityId entityId, const PhysicsComponent& comp);
    void     destroyBody(uint32_t handle);
    /** Destroy every body in the world (editor project swap / hot-reload). */
    void     destroyAllBodies();
    void     setBodyActive(uint32_t handle, bool active);

    // ---- Velocity / position ------------------------------------------------
    void setLinearVelocity(uint32_t handle, const Vec2& vel);
    /** Apply an instantaneous velocity change to a dynamic body. */
    void applyImpulse(uint32_t handle, const Vec2& impulse);
    /** Accumulate acceleration for the next simulation step. */
    void applyForce(uint32_t handle, const Vec2& force);
    /** Per-entity gravity multiplier (0 = ignore world gravity, e.g. top-down). */
    void setGravityScale(uint32_t handle, float scale);
    Vec2 getLinearVelocity(uint32_t handle) const;
    void setPosition(uint32_t handle, const Vec2& pos);
    Vec2 getPosition(uint32_t handle) const;

    // ---- Collision queries --------------------------------------------------
    bool areOverlapping(uint32_t handle1, uint32_t handle2) const;

    struct RaycastResult {
        bool     hit      = false;
        uint32_t handle   = 0;
        EntityId entityId = 0;
        Vec2     point;
        float    distance = 0.f;
    };
    RaycastResult         raycast(const Vec2& from, const Vec2& to) const;
    std::vector<uint32_t> getContactingBodies(const Vec2& point)    const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ArtCade::Modules
