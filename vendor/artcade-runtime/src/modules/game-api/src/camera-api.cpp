// camera-api.cpp — Lua bindings for CameraManager (via Renderer)
//
// Exposes:
//   camera.setPosition(x, y)    — move camera target to world point (x,y)
//   camera.move(dx, dy)         — relative pan
//   camera.setZoom(zoom)        — set zoom (1.0 = normal)
//   camera.zoomBy(delta)        — relative zoom change
//   camera.centerOn(entityId)   — follow an entity
//   camera.x()  / camera.y()   — read current camera target
//   camera.zoom()               — read current zoom
//
// Note: the Renderer stores the Camera2D directly; these calls delegate to
// Renderer::setCameraPosition / setCameraZoom / getCameraPosition / getCameraZoom.

#include "../include/game-api.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../renderer/include/renderer.h"
#include "../../camera-manager/include/camera-manager.h"
#include "../../../world/include/world.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

void GameAPI::bindCameraAPI(sol::state& lua) {
    auto* renderer = ctx_.renderer;
    auto* entities = ctx_.entityGateway;
    auto* camMgr   = ctx_.cameraManager;
    auto* world    = ctx_.world;

    // camera.setPosition(x, y)
    lua.set_function("camera_setPosition", [renderer](float x, float y) {
        if (renderer) renderer->setCameraPosition({ x, y });
    });

    // camera.move(dx, dy)
    lua.set_function("camera_move", [renderer](float dx, float dy) {
        if (!renderer) return;
        auto pos = renderer->getCameraPosition();
        renderer->setCameraPosition({ pos.x + dx, pos.y + dy });
    });

    // camera.setZoom(zoom)
    lua.set_function("camera_setZoom", [renderer](float zoom) {
        if (renderer) renderer->setCameraZoom(zoom);
    });

    // camera.zoomBy(delta) — additive zoom change
    lua.set_function("camera_zoomBy", [renderer](float delta) {
        if (!renderer) return;
        renderer->setCameraZoom(renderer->getCameraZoom() + delta);
    });

    // camera.centerOn(entityId) — set camera target to entity position
    lua.set_function("camera_centerOn", [renderer, entities](EntityId id) {
        if (!renderer || !entities) return;
        Transform transform{};
        if (!entities->getTransform(id, transform)) return;
        renderer->setCameraCenter(transform.position);
    });

    lua.set_function("camera_follow", [world](EntityId id) -> bool {
        return world && world->followCameraTarget(id);
    });
    lua.set_function("camera_stopFollowing", [world]() {
        if (world) world->stopCameraFollow();
    });
    lua.set_function("camera_useDefaultTarget", [world]() {
        if (world) world->useAutomaticCameraTarget();
    });

    // camera.x() / camera.y() — read back camera target
    lua.set_function("camera_x", [renderer]() -> float {
        return renderer ? renderer->getCameraPosition().x : 0.f;
    });
    lua.set_function("camera_y", [renderer]() -> float {
        return renderer ? renderer->getCameraPosition().y : 0.f;
    });
    lua.set_function("camera_getZoom", [renderer]() -> float {
        return renderer ? renderer->getCameraZoom() : 1.f;
    });

    // screen.isOffScreen(id) — true when the entity's position is outside the
    // current camera view (authoritative game camera + visible world size).
    lua.set_function("screen_isOffScreen", [renderer, entities](EntityId id) -> bool {
        if (!renderer || !entities) return true;
        Transform transform{};
        if (!entities->getTransform(id, transform)) return true;
        const Vec2 topLeft = renderer->getCameraPosition();
        const Vec2 visible = renderer->visibleWorldSize();
        const float minX = topLeft.x;
        const float maxX = topLeft.x + visible.x;
        const float minY = topLeft.y;
        const float maxY = topLeft.y + visible.y;
        const Vec2 p = transform.position;
        return p.x < minX || p.x > maxX || p.y < minY || p.y > maxY;
    });

    // camera.shake(trauma) — add screen-shake trauma (0–1, stacks, decays over time)
    lua.set_function("camera_shake", [camMgr](float trauma, sol::optional<float> durationSeconds) {
        if (!camMgr || trauma <= 0.f) return;
        const float duration = durationSeconds.value_or(0.5f);
        camMgr->addTrauma(trauma, duration > 0.f ? duration : 0.5f);
    });

    // Lua-side convenience table
    lua.script(R"(
        camera = {}
        camera.setPosition = function(x, y)   camera_setPosition(x, y)   end
        camera.move        = function(dx, dy)  camera_move(dx, dy)        end
        camera.setZoom     = function(z)       camera_setZoom(z)          end
        camera.zoomBy      = function(d)       camera_zoomBy(d)           end
        camera.centerOn    = function(id)      camera_centerOn(id)        end
        camera.follow      = function(id) return camera_follow(id)       end
        camera.stopFollowing = function() camera_stopFollowing()         end
        camera.useDefaultTarget = function() camera_useDefaultTarget()   end
        camera.x           = function()  return camera_x()               end
        camera.y           = function()  return camera_y()               end
        camera.zoom        = function()  return camera_getZoom()         end
        camera.shake       = function(intensity, duration)
            camera_shake(intensity ~= nil and intensity or 0.5,
                         duration ~= nil and duration or 0.5)
        end

        screen = screen or {}
        screen.isOffScreen = function(id) return screen_isOffScreen(id) end
        screen.isOnScreen  = function(id) return not screen_isOffScreen(id) end
    )");
}

} // namespace ArtCade::Modules
