#include "../include/game-api.h"
#include "../../../world/include/world.h"
#include "../../modules/runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../physics/include/physics.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

namespace {

std::string tableString(sol::table table, const char* key) {
    sol::object value = table[key];
    if (!value.valid() || value == sol::nil)
        return {};
    return value.as<std::string>();
}

CollisionWorld::Filter collisionFilterFrom(sol::object value) {
    CollisionWorld::Filter filter;
    if (!value.valid() || value == sol::nil)
        return filter;
    if (value.is<std::string>()) {
        filter.className = value.as<std::string>();
        return filter;
    }
    if (!value.is<sol::table>())
        return filter;

    sol::table table = value.as<sol::table>();
    filter.layerId   = tableString(table, "layer");
    filter.role      = tableString(table, "role");
    filter.response  = tableString(table, "response");
    filter.className = tableString(table, "className");
    filter.tag       = tableString(table, "tag");
    if (filter.layerId.empty())
        filter.layerId = tableString(table, "layerId");
    return filter;
}

std::string contactKindName(CollisionWorld::ContactEvent::Kind kind) {
    switch (kind) {
    case CollisionWorld::ContactEvent::Kind::Stay: return "stay";
    case CollisionWorld::ContactEvent::Kind::Exit: return "exit";
    case CollisionWorld::ContactEvent::Kind::Enter:
    default: return "enter";
    }
}

sol::table contactEventTable(sol::state_view lua,
                             const CollisionWorld::ContactEvent& event) {
    sol::table tbl = lua.create_table(0, 13);
    tbl["kind"] = contactKindName(event.kind);
    tbl["self"] = static_cast<int>(event.self);
    tbl["other"] = static_cast<int>(event.other);
    tbl["selfRole"] = event.selfRole;
    tbl["otherRole"] = event.otherRole;
    tbl["selfResponse"] = event.selfResponse;
    tbl["otherResponse"] = event.otherResponse;
    tbl["selfLayer"] = event.selfLayerId;
    tbl["otherLayer"] = event.otherLayerId;
    tbl["normalX"] = event.normal.x;
    tbl["normalY"] = event.normal.y;
    tbl["x"] = event.point.x;
    tbl["y"] = event.point.y;
    return tbl;
}

} // namespace

void GameAPI::bindPhysicsAPI(sol::state& lua) {
    auto* entities = ctx_.entityGateway;
    auto* physics = ctx_.physics;
    auto* world = ctx_.world;

    // collision.overlap(id1, id2) → bool (geometric; no physics body required)
    lua.set_function("collision_overlap",
        [entities, world](EntityId id1, EntityId id2) -> bool {
            (void)entities;
            return world && world->collisionOverlap(id1, id2);
        });

    // collision.firstTouching(entityId, filter) -> entityId (0 if none)
    lua.set_function("collision_firstTouching",
        [world](EntityId id, sol::object filterObj) -> int {
            const CollisionWorld::Filter filter = collisionFilterFrom(filterObj);
            if (!world) return 0;
            return static_cast<int>(world->firstCollisionTouching(id, filter));
        });

    lua.set_function("collision_isGrounded",
        [world](EntityId id) -> bool {
            return world && world->collisionGrounded(id);
        });

    lua.set_function("collision_hasEvent",
        [world](EntityId id, const std::string& kind, sol::object filterObj) -> bool {
            if (!world) return false;
            return world->hasCollisionEvent(id, kind, collisionFilterFrom(filterObj));
        });

    lua.set_function("collision_events",
        [world](sol::this_state ts, EntityId id, const std::string& kind, sol::object filterObj) -> sol::object {
            sol::state_view L(ts);
            sol::table out = L.create_table();
            if (!world)
                return sol::make_object(L, out);
            const auto events = world->collisionEventsFor(
                id, kind, collisionFilterFrom(filterObj));
            int index = 1;
            for (const auto& event : events)
                out[index++] = contactEventTable(L, event);
            return sol::make_object(L, out);
        });

    // collision.raycast(x1, y1, x2, y2) → {hit, entityId, x, y, dist}
    lua.set_function("collision_raycast",
        [physics, world](sol::this_state ts, float x1, float y1, float x2, float y2, sol::object filterObj) -> sol::object {
            sol::state_view L(ts);
            sol::table tbl = L.create_table(0, 5);

            if (world) {
                const auto r = world->collisionRaycast(
                    { x1, y1 }, { x2, y2 }, collisionFilterFrom(filterObj));
                tbl["hit"]      = r.hit;
                tbl["entityId"] = static_cast<int>(r.entityId);
                tbl["x"]        = r.point.x;
                tbl["y"]        = r.point.y;
                tbl["dist"]     = r.distance;
                return sol::make_object(L, tbl);
            }

            if (!physics) {
                tbl["hit"]      = false;
                tbl["entityId"] = 0;
                tbl["x"]        = 0.f;
                tbl["y"]        = 0.f;
                tbl["dist"]     = 0.f;
                return sol::make_object(L, tbl);
            }

            auto r = physics->raycast({ x1, y1 }, { x2, y2 });
            tbl["hit"]      = r.hit;
            tbl["entityId"] = static_cast<int>(r.entityId);
            tbl["x"]        = r.point.x;
            tbl["y"]        = r.point.y;
            tbl["dist"]     = r.distance;
            return sol::make_object(L, tbl);
        });

    lua.set_function("physics_createBody",
        [entities, physics](EntityId id,
                      const std::string& bt,
                      const std::string& st,
                      float w, float h) -> uint32_t
        {
            Transform transform{};
            if (!entities->exists(id) || !entities->getTransform(id, transform)) return 0;

            PhysicsComponent comp;
            comp.bodyType =
                (bt == "static")    ? BodyType::Static    :
                (bt == "kinematic") ? BodyType::Kinematic : BodyType::Dynamic;

            comp.collider.shape =
                (st == "circle") ? ColliderShape::Circle : ColliderShape::Rectangle;
            comp.collider.size    = { w, h };
            comp.collider.density  = 1.f;
            comp.collider.friction = 0.4f;
            comp.collider.offset   = { 0.f, 0.f };

            uint32_t handle = physics->createBody(id, comp);
            if (handle == 0) return 0;

            physics->setPosition(handle, transform.position);

            comp.physicsHandle = handle;
            entities->setPhysicsComponent(id, comp);
            entities->setPhysicsHandle(id, handle);

            return handle;
        });

    lua.set_function("physics_setGravity", [physics](float gx, float gy) {
        physics->setGravity({ gx, gy });
    });

    lua.set_function("physics_bodyPosition",
        [entities, physics](EntityId id) -> std::tuple<float,float> {
            const uint32_t handle = entities->physicsHandle(id);
            if (!entities->exists(id) || handle == 0) return {0.f,0.f};
            auto p = physics->getPosition(handle);
            return {p.x, p.y};
        });

    lua.set_function("physics_applyImpulse",
        [entities, physics](EntityId id, float ix, float iy) {
            const uint32_t handle = entities->physicsHandle(id);
            if (!entities->exists(id) || handle == 0) return;
            physics->applyImpulse(handle, { ix, iy });
        });

    lua.set_function("physics_applyForce",
        [entities, physics](EntityId id, float fx, float fy) {
            const uint32_t handle = entities->physicsHandle(id);
            if (!entities->exists(id) || handle == 0) return;
            physics->applyForce(handle, { fx, fy });
        });

    lua.script(R"(
        collision = {}
        collision.overlap       = function(id1, id2)     return collision_overlap(id1, id2)          end
        collision.firstTouching = function(id, filter)   return collision_firstTouching(id, filter)   end
        collision.raycast       = function(x1,y1,x2,y2,filter) return collision_raycast(x1,y1,x2,y2,filter) end
        collision.isGrounded    = function(id)           return collision_isGrounded(id)              end
        collision.hasEvent      = function(id, kind, filter) return collision_hasEvent(id, kind or "stay", filter) end
        collision.events        = function(id, kind, filter) return collision_events(id, kind or "", filter) end

        physics = {}
        physics.createBody    = function(id, bt, st, w, h)
                                    return physics_createBody(id, bt or "dynamic", st or "rect", w or 32, h or 32)
                                end
        physics.setGravity    = function(gx, gy)         return physics_setGravity(gx, gy)           end
        physics.bodyPosition  = function(id)             return physics_bodyPosition(id)              end
        physics.applyImpulse  = function(id, ix, iy)     return physics_applyImpulse(id, ix, iy)      end
        physics.applyForce    = function(id, fx, fy)     return physics_applyForce(id, fx, fy)        end
    )");
}

} // namespace ArtCade::Modules
