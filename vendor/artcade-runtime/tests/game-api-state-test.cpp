#include "core/engine-context.h"
#include "modules/game-api/include/game-api.h"
#include "modules/variable-manager/include/variable-manager.h"

#include <sol/sol.hpp>

#include <cstdio>
#include <cstdlib>
#include <vector>

namespace {

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", message);
}

ArtCade::GameVariableDefinition number_variable(const char* key, double initial_value)
{
    ArtCade::GameVariableDefinition definition;
    definition.key = key;
    definition.type = ArtCade::GameVariableDefinition::Type::Number;
    definition.initialValue = initial_value;
    return definition;
}

} // namespace

int main()
{
    ArtCade::Modules::VariableManager variables;
    variables.init();
    variables.configureGlobals({number_variable("score", 1.0)});

    ArtCade::EngineContext context;
    context.variableManager = &variables;
    ArtCade::Modules::GameAPI api(context);
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    api.registerAll(lua);

    sol::table global = lua["global"];
    sol::protected_function set = global["set"];
    sol::protected_function add = global["add"];

    const sol::protected_function_result set_score = set("score", 4.0);
    expect(set_score.valid() && set_score.get<bool>(), "Lua global.set accepts matching type");
    expect(variables.tryGetNumber("score") == 4.0, "Lua global.set updates declared variable");

    const sol::protected_function_result set_missing = set("missing", 3.0);
    expect(set_missing.valid() && !set_missing.get<bool>(), "Lua global.set rejects unknown variable");

    const sol::protected_function_result add_score = add("score", -4.0);
    expect(add_score.valid() && add_score.get<double>() == 0.0,
           "Lua global.add preserves a valid zero result");

    const sol::protected_function_result add_missing = add("missing", 3.0);
    expect(add_missing.valid() && add_missing.get<sol::object>().is<sol::lua_nil_t>(),
           "Lua global.add returns nil when the mutation is rejected");

    api.shutdown();
    variables.shutdown();
    return 0;
}
