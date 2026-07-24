#include "../include/game-api.h"
#include "../../variable-manager/include/variable-manager.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {
namespace {

sol::object toLua(sol::this_state state, const VariableManager::Value& value) {
    sol::state_view lua(state);
    if (const auto* number = std::get_if<double>(&value)) return sol::make_object(lua, *number);
    if (const auto* boolean = std::get_if<bool>(&value)) return sol::make_object(lua, *boolean);
    if (const auto* string = std::get_if<std::string>(&value)) return sol::make_object(lua, *string);
    return sol::lua_nil;
}

std::optional<VariableManager::Value> fromLua(const sol::object& value) {
    if (value.is<bool>()) return value.as<bool>();
    if (value.is<double>()) return value.as<double>();
    if (value.is<std::string>()) return value.as<std::string>();
    return std::nullopt;
}

} // namespace

void GameAPI::bindStateAPI(sol::state& lua) {
    auto* variables = ctx_.variableManager;

    lua.set_function("global_get", [variables](sol::this_state state, const std::string& key) {
        if (!variables || !variables->exists(key)) return sol::object(sol::lua_nil);
        return toLua(state, variables->get(key));
    });
    lua.set_function("global_set", [variables](const std::string& key, const sol::object& value) {
        const auto converted = fromLua(value);
        return variables && converted && variables->setGlobal(key, *converted).accepted();
    });
    lua.set_function("global_add", [variables](sol::this_state state,
                                                 const std::string& key,
                                                 double amount) -> sol::object {
        sol::state_view lua(state);
        if (!variables) return sol::make_object(lua, sol::lua_nil);
        const auto result = variables->addNumber(key, amount);
        if (!result.accepted() || !std::holds_alternative<double>(result.after))
            return sol::make_object(lua, sol::lua_nil);
        return sol::make_object(lua, std::get<double>(result.after));
    });

    lua.set_function("objectvar_get", [variables](sol::this_state state, EntityId id, const std::string& key) {
        if (!variables || !variables->entityExists(id, key)) return sol::object(sol::lua_nil);
        return toLua(state, variables->getEntity(id, key));
    });
    lua.set_function("objectvar_set", [variables](EntityId id, const std::string& key, const sol::object& value) {
        const auto converted = fromLua(value);
        return variables && converted && variables->setEntity(id, key, *converted);
    });
    lua.set_function("objectvar_add", [variables](EntityId id, const std::string& key, double amount) {
        return variables ? variables->addEntity(id, key, amount).value_or(0.0) : 0.0;
    });

    lua.script(R"(
        global = {
            get = global_get,
            set = global_set,
            add = global_add,
        }
        objectvar = {
            get = objectvar_get,
            set = objectvar_set,
            add = objectvar_add,
        }
    )");
}

} // namespace ArtCade::Modules
