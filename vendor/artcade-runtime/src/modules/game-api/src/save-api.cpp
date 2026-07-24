#include "../include/game-api.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"
#include "../../save-load/include/save-load-manager.h"
#include "../../variable-manager/include/variable-manager.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

void GameAPI::bindSaveAPI(sol::state& lua) {
    auto* saves = ctx_.saveLoadManager;
    auto* variables = ctx_.variableManager;
    auto* entities = ctx_.entityGateway;

    lua.set_function("save_write_game", [saves, variables, entities](const std::string& slot) {
        if (!saves || !variables || !entities) return false;
        return saves->save(slot,
            variables->takeGameSnapshot(entities->persistentEntityIds()));
    });
    lua.set_function("save_load_game", [saves, variables, entities](const std::string& slot) {
        if (!saves || !variables || !entities) return false;
        const auto snapshot = saves->load(slot);
        return snapshot && variables->restoreGameSnapshot(
            *snapshot, entities->persistentEntityIds());
    });
    lua.set_function("save_exists", [saves](const std::string& slot) {
        return saves && saves->hasSave(slot);
    });
    lua.set_function("save_delete", [saves](const std::string& slot) {
        if (saves) saves->deleteSave(slot);
    });
    lua.set_function("save_list", [saves](sol::this_state ts) {
        sol::state_view state(ts);
        sol::table result = state.create_table();
        if (saves) {
            const auto slots = saves->listSlots();
            for (size_t i = 0; i < slots.size(); ++i)
                result[static_cast<int>(i) + 1] = slots[i];
        }
        return result;
    });

    lua.script(R"(
        save = {}
        save.writeGame = function(slot) return save_write_game(slot) end
        save.loadGame = function(slot) return save_load_game(slot) end
        save.exists = function(slot) return save_exists(slot) end
        save.delete = function(slot) save_delete(slot) end
        save.list = function() return save_list() end
    )");
}

} // namespace ArtCade::Modules
