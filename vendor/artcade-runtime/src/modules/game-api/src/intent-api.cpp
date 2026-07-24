#include "../include/game-api.h"
#include "../../../world/include/world.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

void GameAPI::bindIntentAPI(sol::state& lua) {
    auto* world = ctx_.world;

    lua.set_function("movement_setIntent",
        [world](EntityId id, float x, float y) {
            if (world) world->setMovementIntent(id, x, y);
        });
    lua.set_function("movement_clearIntent",
        [world](EntityId id) {
            if (world) world->clearMovementIntent(id);
        });
    lua.set_function("platformer_requestJump",
        [world](EntityId id) {
            if (world) world->requestJump(id);
        });

    lua.script(R"(
        movement = movement or {}
        platformer = platformer or {}

        function movement.setIntent(entityId, directionX, directionY)
            return movement_setIntent(entityId, directionX or 0, directionY or 0)
        end

        function movement.clearIntent(entityId)
            return movement_clearIntent(entityId)
        end

        function platformer.requestJump(entityId)
            return platformer_requestJump(entityId)
        end
    )");
}

} // namespace ArtCade::Modules
