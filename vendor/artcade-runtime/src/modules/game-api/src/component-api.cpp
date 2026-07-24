#include "../include/game-api.h"
#include "component-value-api.h"
#include "../../../world/include/world.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"

#include <algorithm>
#include <cmath>
#include <sol/sol.hpp>

namespace ArtCade::Modules {

namespace {

RuntimeEntityGateway* gateway(const EngineContext& ctx) {
    return ctx.entityGateway;
}

World* world(const EngineContext& ctx) {
    return ctx.world;
}

} // namespace

void GameAPI::bindComponentAPI(sol::state& lua) {
    const EngineContext& ctx = ctx_;
    bindComponentValueAPI(lua, ctx);

    lua.set_function("linearMover_setDirection",
        [ctx](EntityId id, float dx, float dy) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            LinearMoverComponent lm{};
            if (!gw->getLinearMover(id, lm)) return false;
            lm.directionX = dx;
            lm.directionY = dy;
            return gw->setLinearMover(id, lm);
        });

    lua.set_function("linearMover_setSpeed",
        [ctx](EntityId id, float speed) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            LinearMoverComponent lm{};
            if (!gw->getLinearMover(id, lm)) return false;
            lm.speed = std::max(0.f, speed);
            return gw->setLinearMover(id, lm);
        });

    lua.set_function("linearMover_setPaused",
        [ctx](EntityId id, bool paused) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            LinearMoverComponent lm{};
            if (!gw->getLinearMover(id, lm)) return false;
            lm._paused = paused;
            return gw->setLinearMover(id, lm);
        });

    lua.set_function("magnet_setEnabled",
        [ctx](EntityId id, bool enabled) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            MagneticItemComponent mag{};
            if (!gw->getMagneticItem(id, mag)) return false;
            mag._enabled = enabled;
            return gw->setMagneticItem(id, mag);
        });

    lua.set_function("magnet_setTargetTag",
        [ctx](EntityId id, const std::string& tag) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            MagneticItemComponent mag{};
            if (!gw->getMagneticItem(id, mag)) return false;
            mag.attractTag = tag;
            return gw->setMagneticItem(id, mag);
        });

    lua.set_function("magnet_setRadius",
        [ctx](EntityId id, float radius) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            MagneticItemComponent magnet{};
            if (!gw->getMagneticItem(id, magnet)) return false;
            magnet.radius = std::max(0.f, radius);
            return gw->setMagneticItem(id, magnet);
        });

    lua.set_function("magnet_setPullSpeed",
        [ctx](EntityId id, float speed) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            MagneticItemComponent magnet{};
            if (!gw->getMagneticItem(id, magnet)) return false;
            magnet.pullSpeed = std::max(0.f, speed);
            return gw->setMagneticItem(id, magnet);
        });

    lua.set_function("horde_setTargetClass",
        [ctx](EntityId id, const std::string& className) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            HordeMemberComponent horde{};
            if (!gw->getHordeMember(id, horde)) return false;
            horde.targetClass = className;
            return gw->setHordeMember(id, horde);
        });

    lua.set_function("horde_setWeights",
        [ctx](EntityId id, float chaseWeight, float separationWeight) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            HordeMemberComponent horde{};
            if (!gw->getHordeMember(id, horde)) return false;
            horde.chaseWeight = std::max(0.f, chaseWeight);
            horde.separationWeight = std::max(0.f, separationWeight);
            return gw->setHordeMember(id, horde);
        });

    lua.set_function("horde_setMaxSpeed",
        [ctx](EntityId id, float speed) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            HordeMemberComponent horde{};
            if (!gw->getHordeMember(id, horde)) return false;
            horde.maxSpeed = std::max(0.f, speed);
            return gw->setHordeMember(id, horde);
        });

    lua.set_function("horde_setSeparationRadius",
        [ctx](EntityId id, float radius) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            HordeMemberComponent horde{};
            if (!gw->getHordeMember(id, horde)) return false;
            horde.separationRadius = std::max(0.f, radius);
            return gw->setHordeMember(id, horde);
        });

    lua.set_function("autoDestroy_setLifespan",
        [ctx](EntityId id, float lifespan) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            AutoDestroyComponent ad{};
            if (!gw->getAutoDestroy(id, ad)) return false;
            ad.lifespan = std::max(0.f, lifespan);
            ad._timeAlive = 0.f;
            return gw->setAutoDestroy(id, ad);
        });

    lua.set_function("autoDestroy_cancel",
        [ctx](EntityId id) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            AutoDestroyComponent ad{};
            if (!gw->getAutoDestroy(id, ad)) return false;
            ad.lifespan = 0.f;
            ad._timeAlive = 0.f;
            return gw->setAutoDestroy(id, ad);
        });

    lua.set_function("platformer_setMaxSpeed",
        [ctx](EntityId id, float speed) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            PlatformerControllerComponent pc{};
            if (!gw->getPlatformerController(id, pc)) return false;
            pc.maxSpeed = std::max(0.f, speed);
            return gw->setPlatformerController(id, pc);
        });

    lua.set_function("platformer_setJumpForce",
        [ctx](EntityId id, float force) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            PlatformerControllerComponent pc{};
            if (!gw->getPlatformerController(id, pc)) return false;
            pc.jumpForce = std::max(0.f, force);
            return gw->setPlatformerController(id, pc);
        });

    lua.set_function("platformer_setGravity",
        [ctx](EntityId id, float gravity) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            PlatformerControllerComponent pc{};
            if (!gw->getPlatformerController(id, pc)) return false;
            pc.customGravity = gravity;
            return gw->setPlatformerController(id, pc);
        });

    lua.set_function("topDown_setMaxSpeed",
        [ctx](EntityId id, float speed) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            TopDownControllerComponent td{};
            if (!gw->getTopDownController(id, td)) return false;
            td.maxSpeed = std::max(0.f, speed);
            return gw->setTopDownController(id, td);
        });

    lua.set_function("topDown_setAcceleration",
        [ctx](EntityId id, float acceleration) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            TopDownControllerComponent td{};
            if (!gw->getTopDownController(id, td)) return false;
            td.acceleration = std::max(0.f, acceleration);
            return gw->setTopDownController(id, td);
        });

    lua.set_function("topDown_setFriction",
        [ctx](EntityId id, float friction) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            TopDownControllerComponent td{};
            if (!gw->getTopDownController(id, td)) return false;
            td.friction = std::max(0.f, friction);
            return gw->setTopDownController(id, td);
        });

    lua.set_function("topDown_setFourDirections",
        [ctx](EntityId id, bool enabled) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            TopDownControllerComponent td{};
            if (!gw->getTopDownController(id, td)) return false;
            td.fourDirections = enabled;
            return gw->setTopDownController(id, td);
        });

    lua.set_function("text_setText",
        [ctx](EntityId id, const std::string& str) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            TextComponent tc{};
            if (!gw->getText(id, tc)) return false;
            tc.text = str;
            return gw->setText(id, tc);
        });

    lua.set_function("text_setColor",
        [ctx](EntityId id, float r, float g, float b, float a) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            TextComponent tc{};
            if (!gw->getText(id, tc)) return false;
            tc.color = { r, g, b, a };
            return gw->setText(id, tc);
        });

    lua.set_function("text_setSize",
        [ctx](EntityId id, int size) -> bool {
            auto* gw = gateway(ctx);
            if (!gw) return false;
            TextComponent tc{};
            if (!gw->getText(id, tc)) return false;
            tc.size = size > 1 ? size : 1;
            return gw->setText(id, tc);
        });

    lua.set_function("platformer_isGrounded",
        [ctx](EntityId id) -> bool {
            auto* w = world(ctx);
            return w && w->isPlatformerGrounded(id);
        });

    lua.script(R"(
        component = component or {}
        platformer = platformer or {}
        topDown = topDown or {}
        text = text or {}
        linearMover = linearMover or {}
        magnet = magnet or {}
        horde = horde or {}
        autoDestroy = autoDestroy or {}

        function component.value(entityId, property)
            return component_value(entityId, property or "")
        end

        function linearMover.setDirection(entityId, directionX, directionY)
            return linearMover_setDirection(entityId, directionX or 0, directionY or 0)
        end

        function linearMover.setSpeed(entityId, speed)
            return linearMover_setSpeed(entityId, speed or 0)
        end

        function linearMover.pause(entityId)
            return linearMover_setPaused(entityId, true)
        end

        function linearMover.resume(entityId)
            return linearMover_setPaused(entityId, false)
        end

        function magnet.setEnabled(entityId, enabled)
            return magnet_setEnabled(entityId, enabled ~= false)
        end

        function magnet.setTargetTag(entityId, tag)
            return magnet_setTargetTag(entityId, tag or "")
        end

        function magnet.setRadius(entityId, radius)
            return magnet_setRadius(entityId, radius or 0)
        end

        function magnet.setPullSpeed(entityId, speed)
            return magnet_setPullSpeed(entityId, speed or 0)
        end

        function horde.setTargetClass(entityId, className)
            return horde_setTargetClass(entityId, className or "")
        end

        function horde.setWeights(entityId, chaseWeight, separationWeight)
            return horde_setWeights(entityId, chaseWeight or 0, separationWeight or 0)
        end

        function horde.setMaxSpeed(entityId, speed)
            return horde_setMaxSpeed(entityId, speed or 0)
        end

        function horde.setSeparationRadius(entityId, radius)
            return horde_setSeparationRadius(entityId, radius or 0)
        end

        function autoDestroy.setLifespan(entityId, lifespan)
            return autoDestroy_setLifespan(entityId, lifespan or 0)
        end

        function autoDestroy.cancel(entityId)
            return autoDestroy_cancel(entityId)
        end

        function platformer.isGrounded(entityId)
            return platformer_isGrounded(entityId)
        end

        function platformer.setMaxSpeed(entityId, speed)
            return platformer_setMaxSpeed(entityId, speed or 0)
        end

        function platformer.setJumpForce(entityId, force)
            return platformer_setJumpForce(entityId, force or 0)
        end

        function platformer.setGravity(entityId, gravity)
            return platformer_setGravity(entityId, gravity or 0)
        end

        function topDown.setMaxSpeed(entityId, speed)
            return topDown_setMaxSpeed(entityId, speed or 0)
        end

        function topDown.setAcceleration(entityId, acceleration)
            return topDown_setAcceleration(entityId, acceleration or 0)
        end

        function topDown.setFriction(entityId, friction)
            return topDown_setFriction(entityId, friction or 0)
        end

        function topDown.setFourDirections(entityId, enabled)
            return topDown_setFourDirections(entityId, enabled ~= false)
        end

        function text.set(entityId, str)
            return text_setText(entityId, str or "")
        end

        function text.setColor(entityId, r, g, b, a)
            return text_setColor(entityId, r or 1, g or 1, b or 1, a or 1)
        end

        function text.setSize(entityId, size)
            return text_setSize(entityId, size or 24)
        end
    )");
}

} // namespace ArtCade::Modules
