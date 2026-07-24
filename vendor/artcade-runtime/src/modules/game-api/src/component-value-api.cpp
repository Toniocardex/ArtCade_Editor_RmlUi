#include "component-value-api.h"

#include <algorithm>

#include "../../../core/engine-context.h"
#include "../../../world/include/world.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

void bindComponentValueAPI(sol::state& lua, const EngineContext& ctx) {
    auto* gw = ctx.entityGateway;
    auto* world = ctx.world;
    lua.set_function("component_value",
        [gw, world](EntityId id, const std::string& property,
              sol::this_state ts) -> sol::object {
            sol::state_view state(ts);
            if (!gw) return sol::make_object(state, sol::lua_nil);

            PlatformerControllerComponent platformer{};
            if (gw->getPlatformerController(id, platformer)) {
                if (property == "platformer.maxSpeed") return sol::make_object(state, platformer.maxSpeed);
                if (property == "platformer.jumpForce") return sol::make_object(state, platformer.jumpForce);
                if (property == "platformer.customGravity") return sol::make_object(state, platformer.customGravity);
                if (property == "platformer.coyoteTime") return sol::make_object(state, platformer.coyoteTime);
                if (property == "platformer.jumpBuffer") return sol::make_object(state, platformer.jumpBuffer);
                if (property == "platformer.grounded") {
                    return sol::make_object(
                        state, world && world->isPlatformerGrounded(id));
                }
            }

            TopDownControllerComponent top_down{};
            if (gw->getTopDownController(id, top_down)) {
                if (property == "topDown.maxSpeed") return sol::make_object(state, top_down.maxSpeed);
                if (property == "topDown.acceleration") return sol::make_object(state, top_down.acceleration);
                if (property == "topDown.friction") return sol::make_object(state, top_down.friction);
                if (property == "topDown.fourDirections") return sol::make_object(state, top_down.fourDirections);
            }

            LinearMoverComponent mover{};
            if (gw->getLinearMover(id, mover)) {
                if (property == "linearMover.directionX") return sol::make_object(state, mover.directionX);
                if (property == "linearMover.directionY") return sol::make_object(state, mover.directionY);
                if (property == "linearMover.speed") return sol::make_object(state, mover.speed);
                if (property == "linearMover.paused") return sol::make_object(state, mover._paused);
            }

            CameraTargetComponent camera_target{};
            if (gw->getCameraTarget(id, camera_target)) {
                if (property == "cameraTarget.offsetX") return sol::make_object(state, camera_target.offsetX);
                if (property == "cameraTarget.offsetY") return sol::make_object(state, camera_target.offsetY);
                if (property == "cameraTarget.followSpeed") return sol::make_object(state, camera_target.followSpeed);
            }

            MagneticItemComponent magnet{};
            if (gw->getMagneticItem(id, magnet)) {
                if (property == "magnet.enabled") return sol::make_object(state, magnet._enabled);
                if (property == "magnet.attractTag") return sol::make_object(state, magnet.attractTag);
                if (property == "magnet.radius") return sol::make_object(state, magnet.radius);
                if (property == "magnet.pullSpeed") return sol::make_object(state, magnet.pullSpeed);
            }

            HordeMemberComponent horde{};
            if (gw->getHordeMember(id, horde)) {
                if (property == "horde.targetClass") return sol::make_object(state, horde.targetClass);
                if (property == "horde.maxSpeed") return sol::make_object(state, horde.maxSpeed);
                if (property == "horde.separationRadius") return sol::make_object(state, horde.separationRadius);
                if (property == "horde.separationWeight") return sol::make_object(state, horde.separationWeight);
                if (property == "horde.chaseWeight") return sol::make_object(state, horde.chaseWeight);
            }

            AutoDestroyComponent auto_destroy{};
            if (gw->getAutoDestroy(id, auto_destroy)) {
                if (property == "autoDestroy.lifespan") return sol::make_object(state, auto_destroy.lifespan);
                if (property == "autoDestroy.elapsed") return sol::make_object(state, auto_destroy._timeAlive);
                if (property == "autoDestroy.remaining") {
                    return sol::make_object(
                        state,
                        std::max(0.f, auto_destroy.lifespan - auto_destroy._timeAlive));
                }
            }

            TextComponent text{};
            if (gw->getText(id, text)) {
                if (property == "text.text") return sol::make_object(state, text.text);
                if (property == "text.size") return sol::make_object(state, text.size);
                if (property == "text.align") return sol::make_object(state, text.align);
            }

            return sol::make_object(state, sol::lua_nil);
        });
}

} // namespace ArtCade::Modules
