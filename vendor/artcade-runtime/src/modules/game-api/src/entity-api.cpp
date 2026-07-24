#include "../include/game-api.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../physics/include/physics.h"
#include "../../asset-system/include/asset-loader.h"

#include <sol/sol.hpp>
#include <cmath>
#include <tuple>

namespace ArtCade::Modules {

void GameAPI::bindEntityAPI(sol::state& lua) {
    auto* entities = ctx_.entityGateway;
    auto* physics  = ctx_.physics;
    auto* assets   = ctx_.assetLoader;

    // entity.position(id) → x, y
    lua.set_function("entity_position", [entities](EntityId id) -> std::tuple<float, float> {
        Transform transform{};
        if (entities->getTransform(id, transform))
            return { transform.position.x, transform.position.y };
        return { 0.f, 0.f };
    });

    // entity.setPosition(id, x, y)
    lua.set_function("entity_setPosition", [entities, physics](EntityId id, float x, float y) {
        Transform transform{};
        if (!entities->getTransform(id, transform)) return;
        transform.position = { x, y };
        entities->setTransform(id, transform);
        if (physics) {
            const uint32_t handle = entities->physicsHandle(id);
            if (handle != 0)
                physics->setPosition(handle, transform.position);
        }
    });

    // entity.velocity(id) → vx, vy
    lua.set_function("entity_velocity", [entities, physics](EntityId id) -> std::tuple<float, float> {
        if (entities->exists(id)) {
            const uint32_t handle = entities->physicsHandle(id);
            if (handle == 0) return { 0.f, 0.f };
            auto vel = physics->getLinearVelocity(handle);
            return { vel.x, vel.y };
        }
        return { 0.f, 0.f };
    });

    // entity.setVelocity(id, vx, vy)
    lua.set_function("entity_setVelocity", [entities, physics](EntityId id, float vx, float vy) {
        if (entities->exists(id)) {
            const uint32_t handle = entities->physicsHandle(id);
            if (handle != 0)
                physics->setLinearVelocity(handle, { vx, vy });
        }
    });

    // entity.destroy(id)
    // Cleans up the physics body (if any) before removing the entity.
    lua.set_function("entity_destroy", [entities](EntityId id) {
        entities->queueDestroy(id);
    });

    // entity.setRotation(id, radians)
    lua.set_function("entity_setRotation", [entities](EntityId id, float a) {
        Transform transform{};
        if (!entities->getTransform(id, transform)) return;
        transform.rotation = a;
        entities->setTransform(id, transform);
    });
    // entity.setScale(id, sx, sy) — magnitude only; facing lives in flip flags,
    // so Set Scale never clobbers a flip set via Set Flip.
    lua.set_function("entity_setScale", [entities](EntityId id, float sx, float sy) {
        Transform transform{};
        if (!entities->getTransform(id, transform)) return;
        transform.scale = { std::abs(sx), std::abs(sy) };
        entities->setTransform(id, transform);
    });

    // entity.flipX(id) / entity.flipY(id) → current facing flag
    lua.set_function("entity_get_flip_x", [entities](EntityId id) -> bool {
        SpriteComponent sprite{};
        return entities->getSprite(id, sprite) && sprite.flipX;
    });
    lua.set_function("entity_get_flip_y", [entities](EntityId id) -> bool {
        SpriteComponent sprite{};
        return entities->getSprite(id, sprite) && sprite.flipY;
    });
    // entity.setFlip(id, flipX, flipY) — set the facing flags directly
    lua.set_function("entity_set_flip", [entities](EntityId id, bool fx, bool fy) {
        SpriteComponent sprite{};
        if (!entities->getSprite(id, sprite)) return;
        sprite.flipX = fx;
        sprite.flipY = fy;
        entities->setSprite(id, sprite);
    });

    // entity.scale(id) → sx, sy
    lua.set_function("entity_scale", [entities](EntityId id) -> std::tuple<float, float> {
        Transform transform{};
        if (entities->getTransform(id, transform))
            return { transform.scale.x, transform.scale.y };
        return { 1.f, 1.f };
    });

    lua.set_function("entity_imagePoint",
        [entities, assets](EntityId id, const std::string& pointId) -> std::tuple<float, float> {
            if (!entities || !entities->exists(id)) return { 0.f, 0.f };
            Transform transform{};
            if (!entities->getTransform(id, transform)) return { 0.f, 0.f };
            SpriteComponent sprite{};
            if (!entities->getSprite(id, sprite)) return { 0.f, 0.f };
            PhysicsComponent physicsComponent{};
            if (!entities->getPhysicsComponent(id, physicsComponent)) return { 0.f, 0.f };
            float px = 0.5f, py = 0.5f;
            if (assets) {
                if (auto pt = assets->getImagePoint(sprite.spriteAssetId, pointId)) {
                    px = pt->x;
                    py = pt->y;
                }
            }
            float w = 32.f, h = 32.f;
            if (physicsComponent.collider.shape == ColliderShape::Rectangle) {
                w = physicsComponent.collider.size.x;
                h = physicsComponent.collider.size.y;
            } else {
                w = h = physicsComponent.collider.size.x * 2.f;
            }
            const float sx = std::abs(transform.scale.x);
            const float sy = std::abs(transform.scale.y);
            const float lx = (px - sprite.pivot.x) * w * sx;
            const float ly = (py - sprite.pivot.y) * h * sy;
            return { transform.position.x + lx, transform.position.y + ly };
        });
    // entity.setVisible(id, bool) — implemented via sprite alpha (no struct change)
    lua.set_function("entity_setVisible", [entities](EntityId id, bool v) {
        SpriteComponent sprite{};
        if (!entities->getSprite(id, sprite)) return;
        sprite.alpha = v ? 1.f : 0.f;
        entities->setSprite(id, sprite);
    });
    // entity.setTint(id, r, g, b, a)  — components in 0..1
    lua.set_function("entity_setTint",
        [entities](EntityId id, float r, float g, float b, float a) {
            SpriteComponent sprite{};
            if (!entities->getSprite(id, sprite)) return;
            sprite.tint = { r, g, b, a };
            entities->setSprite(id, sprite);
        });

    // scene.load(name) / scene.restart()  — flow control via the gateway
    lua.set_function("scene_load",
        [entities](const std::string& name, sol::optional<float> fadeSec) {
            if (!entities) return;
            const float fade = fadeSec.value_or(0.f);
            if (fade > 0.f)
                entities->requestLoadScene(name, fade);
            else
                entities->loadScene(name);
        });
    lua.set_function("scene_reactivate", [entities](sol::optional<float> fadeSec) {
        if (!entities) return;
        entities->requestSceneReactivation(fadeSec.value_or(0.f));
    });
    lua.set_function("scene_restart", [entities](sol::optional<float> fadeSec) {
        if (!entities) return;
        entities->requestRestartScene(fadeSec.value_or(0.f));
    });

    // pool.getAll(className) → table of ids
    lua.set_function("pool_getAll", [entities](const std::string& cls) {
        return entities->poolByClass(cls);
    });

    // pool.count(className) → integer
    lua.set_function("pool_count", [entities](const std::string& cls) -> size_t {
        return entities->poolCount(cls);
    });

    // -------------------------------------------------------------------------
    // Object.spawn(className, x, y) → EntityId
    // Clones the project template (sprite, physics, tags, …); spawnFromClass logs to console.
    // -------------------------------------------------------------------------
    lua.set_function("object_spawn",
        [entities](const std::string& cls, float x, float y) -> EntityId {
            return entities->spawnFromClass(cls, x, y);
        });

    lua.set_function("object_destroy", [entities](EntityId id) {
        entities->queueDestroy(id);
    });

    // Object.findByTag(tag) → array of EntityIds
    lua.set_function("object_findByTag", [entities](const std::string& tag) {
        return entities->byTag(tag);
    });

    // Object.findNear(x, y, radius, className?) → array of EntityIds
    lua.set_function("object_findNear",
        [entities](sol::this_state ts, float x, float y, float radius,
             sol::optional<std::string> cls) -> sol::object
        {
            sol::state_view L(ts);
            sol::table result = L.create_table();

            std::vector<EntityId> candidates =
                cls ? entities->poolByClass(*cls) : entities->allIds();

            int idx = 1;
            float r2 = radius * radius;
            for (EntityId eid : candidates) {
                Transform transform{};
                if (!entities->getTransform(eid, transform)) continue;
                float dx = transform.position.x - x;
                float dy = transform.position.y - y;
                if (dx*dx + dy*dy <= r2)
                    result[idx++] = eid;
            }
            return sol::make_object(L, result);
        });

    // Object.exists(id) → bool
    lua.set_function("object_exists", [entities](EntityId id) -> bool {
        return entities->exists(id);
    });

    // Object.distance(id1, id2) → float
    lua.set_function("object_distance",
        [entities](EntityId id1, EntityId id2) -> float {
            Transform t1{};
            Transform t2{};
            if (!entities->getTransform(id1, t1) || !entities->getTransform(id2, t2))
                return -1.f;
            float dx = t1.position.x - t2.position.x;
            float dy = t1.position.y - t2.position.y;
            return std::sqrt(dx*dx + dy*dy);
        });

    // Expose Lua-side convenience table wrappers
    lua.script(R"(
        entity = {}
        entity.position    = function(id)       return entity_position(id)       end
        entity.setPosition = function(id, x, y) return entity_setPosition(id,x,y) end
        entity.velocity    = function(id)       return entity_velocity(id)       end
        entity.setVelocity = function(id,vx,vy) return entity_setVelocity(id,vx,vy) end
        entity.destroy     = function(id)       return entity_destroy(id)        end
        entity.setRotation = function(id,a)     return entity_setRotation(id,a)  end
        entity.setScale    = function(id,sx,sy) return entity_setScale(id,sx,sy) end
        entity.scale       = function(id)       return entity_scale(id)           end
        entity.flipX       = function(id)       return entity_get_flip_x(id)      end
        entity.flipY       = function(id)       return entity_get_flip_y(id)      end
        entity.setFlip     = function(id, mx, my)
            -- Per-axis facing flag: 'keep'/nil leave it, 'normal' un-mirror,
            -- 'mirror' mirror, 'toggle' invert. Legacy booleans: true=mirror,
            -- false=normal. Decoupled from scale (which is pure magnitude now).
            local fx, fy = entity_get_flip_x(id), entity_get_flip_y(id)
            local function resolve(m, cur)
                if m == true or m == 'mirror' then return true
                elseif m == false or m == 'normal' then return false
                elseif m == 'toggle' then return not cur
                else return cur end
            end
            entity_set_flip(id, resolve(mx, fx), resolve(my, fy))
        end
        entity.imagePoint  = function(id, pt)   return entity_imagePoint(id, pt)  end
        entity.setVisible  = function(id,v)     return entity_setVisible(id,v)   end
        entity.setTint     = function(id,r,g,b,a) return entity_setTint(id,r,g,b,a) end
        scene = {}
        scene.load       = function(name, fade) return scene_load(name, fade) end
        scene.reactivate = function(fade)      return scene_reactivate(fade) end
        scene.restart    = function(fade)      return scene_restart(fade) end

        pool = {}
        pool.getAll = function(cls)  return pool_getAll(cls)  end
        pool.count  = function(cls)  return pool_count(cls)   end

        object = {}
        object.spawn     = function(cls, x, y)          return object_spawn(cls, x, y)         end
        object.destroy   = function(id)                  return object_destroy(id)               end
        object.findByTag = function(tag)                 return object_findByTag(tag)            end
        object.findNear  = function(x, y, r, cls)        return object_findNear(x, y, r, cls)   end
        object.exists    = function(id)                  return object_exists(id)               end
        object.distance  = function(id1, id2)            return object_distance(id1, id2)        end
    )");
}

} // namespace ArtCade::Modules
