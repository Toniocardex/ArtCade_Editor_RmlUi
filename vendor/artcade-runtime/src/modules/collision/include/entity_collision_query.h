#pragma once

// entity_collision_query.h - Entity-vs-entity overlap from Transform + collision shapes.

#include "collision_math.h"
#include "collision_world.h"
#include "../../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::CollisionQuery {

constexpr float kDefaultColliderSize = 32.f;

inline Vec2 colliderWorldSize(const Modules::RuntimeEntityGateway& gateway,
                              EntityId id)
{
    Transform transform{};
    gateway.getTransform(id, transform);
    PhysicsComponent comp{};
    gateway.getPhysicsComponent(id, comp);

    const bool hasExplicit =
        comp.collider.size.x > 2.f || comp.collider.size.y > 2.f;
    const Vec2 base = hasExplicit
        ? comp.collider.size
        : Vec2{ kDefaultColliderSize, kDefaultColliderSize };
    if (hasExplicit) {
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

inline PhysicsMath::ShapeInstance shapeFromEntity(
    const Modules::RuntimeEntityGateway& gateway,
    EntityId id)
{
    PhysicsMath::ShapeInstance s;
    Transform transform{};
    if (!gateway.getTransform(id, transform))
        return s;

    PhysicsComponent comp{};
    gateway.getPhysicsComponent(id, comp);

    s.position = transform.position;
    s.offset   = comp.collider.offset;
    s.shape    = comp.collider.shape;

    const bool hasExplicit =
        comp.collider.size.x > 2.f || comp.collider.size.y > 2.f;
    const Vec2 worldSize = colliderWorldSize(gateway, id);
    if (s.shape == ColliderShape::Circle) {
        const float r = hasExplicit
            ? std::max(0.5f, comp.collider.size.x)
            : worldSize.x * 0.5f;
        s.size = { r, r };
    } else {
        s.size = worldSize;
    }
    return s;
}

inline bool entityParticipatesInOverlapQuery(
    const Modules::RuntimeEntityGateway& gateway,
    EntityId id)
{
    return gateway.exists(id) && gateway.isEntityActiveInScene(id);
}

inline bool entitiesOverlap(const Modules::RuntimeEntityGateway& gateway,
                          EntityId id1,
                          EntityId id2)
{
    if (id1 == id2) return false;
    if (!entityParticipatesInOverlapQuery(gateway, id1)) return false;
    if (!entityParticipatesInOverlapQuery(gateway, id2)) return false;
    Transform t1{};
    Transform t2{};
    CollisionBodyComponent b1{};
    CollisionBodyComponent b2{};
    if (gateway.getTransform(id1, t1) && gateway.getTransform(id2, t2)
        && gateway.getCollisionBody(id1, b1) && gateway.getCollisionBody(id2, b2)) {
        CollisionWorld::World world;
        world.setLayers({});
        world.addEntity(id1, t1, b1);
        world.addEntity(id2, t2, b2);
        return world.overlapEntities(id1, id2);
    }
    return PhysicsMath::shapesOverlap(shapeFromEntity(gateway, id1),
                                      shapeFromEntity(gateway, id2));
}

} // namespace ArtCade::CollisionQuery
