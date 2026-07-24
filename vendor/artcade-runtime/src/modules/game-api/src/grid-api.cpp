#include "../include/game-api.h"
#include "../../../world/include/world.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

void GameAPI::bindGridAPI(sol::state& lua) {
    auto* world = ctx_.world;

    lua.set_function("grid_snapToGrid", [world](EntityId id, float cellSize) {
        if (world) world->snapEntityToGrid(id, cellSize);
    });
    lua.set_function("grid_moveByOffset", [world](EntityId id, float dx, float dy) {
        if (world) world->moveEntityByOffset(id, dx, dy);
    });
    lua.set_function("grid_isSpaceFree", [world](float x, float y, float w, float h) -> bool {
        return world ? world->isSpaceFree(x, y, w, h) : true;
    });

    lua.script(R"(
        grid = {}
        grid.snapToGrid    = function(id, size) grid_snapToGrid(id, size) end
        grid.moveByOffset  = function(id, dx, dy) grid_moveByOffset(id, dx, dy) end
        grid.isSpaceFree   = function(x, y, w, h) return grid_isSpaceFree(x, y, w, h) end
    )");
}

} // namespace ArtCade::Modules
