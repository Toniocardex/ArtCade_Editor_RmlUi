#include "../include/game-api.h"
#include "../../runtime-entity-gateway/include/runtime-entity-gateway.h"

#include <sol/sol.hpp>

namespace ArtCade::Modules {

// ---------------------------------------------------------------------------
// lifecycle-api.cpp — Lua-facing bindings for entity lifecycle events.
//
// Two layers:
//   1. lifecycle.pollDestroyed()        — legacy snapshot-based polling,
//      kept for scripts that already consume the destroyBuffer_.
//   2. lifecycle.onSpawn(class, fn)     — reactive handlers, dispatched by
//      lifecycle.onDestroy(class, fn)     GameAPI::dispatchLifecycleEvents()
//                                         from the C++ main loop, fed by
//                                         the EnTT on_construct/on_destroy
//                                         <Identity> signals.
//
// The handler tables live on the Lua side (lifecycle._onSpawn /
// _onDestroy keyed by className) so they survive a Lua hot-reload only
// if the user code re-registers them. That's intentional: hot-reload is
// a "scripts reload from scratch" event in ArtCade.
// ---------------------------------------------------------------------------

void GameAPI::bindLifecycleAPI(sol::state& lua) {
    auto* gw = ctx_.entityGateway;

    lua.set_function("lifecycle_pollDestroyed",
        [gw](sol::this_state ts) -> sol::table {
            sol::state_view L(ts);
            sol::table out = L.create_table();
            if (!gw) return out;
            const auto events = gw->pollDestroyed();
            int i = 1;
            for (const auto& ev : events) {
                sol::table row = L.create_table();
                row["entityId"] = ev.entityId;
                out[i++] = row;
            }
            return out;
        });

    lua.script(R"(
        lifecycle = lifecycle or {}
        lifecycle._onSpawn   = {}
        lifecycle._onDestroy = {}

        lifecycle.pollDestroyed = function()
            -- Deprecated: prefer lifecycle.onDestroy(className, fn) for event-first rules.
            return lifecycle_pollDestroyed()
        end

        local function register(bag, className, fn)
            assert(type(className) == "string" and className ~= "",
                "lifecycle: className must be a non-empty string")
            assert(type(fn) == "function",
                "lifecycle: handler must be a function")
            bag[className] = bag[className] or {}
            table.insert(bag[className], fn)
        end

        function lifecycle.onSpawn(className, fn)
            register(lifecycle._onSpawn, className, fn)
        end

        function lifecycle.onDestroy(className, fn)
            register(lifecycle._onDestroy, className, fn)
        end

        function lifecycle.clearHandlers()
            lifecycle._onSpawn   = {}
            lifecycle._onDestroy = {}
        end
    )");
}

// ---------------------------------------------------------------------------
// Dispatcher — called every fixed step by the main loop.
//
// We resolve `lifecycle._onSpawn[className]` / `_onDestroy[className]` once
// per event and invoke each handler in registration order. Errors inside a
// handler are logged via debug.log but don't abort the dispatch (one bad
// script shouldn't take down the rest of the game's reactive logic).
// ---------------------------------------------------------------------------
uint32_t GameAPI::dispatchLifecycleEvents() {
    if (!luaState_ || !ctx_.entityGateway) return 0;

    std::vector<LifecycleEvent> events;
    ctx_.entityGateway->drainLifecycleEvents(events);
    if (events.empty()) return 0;

    sol::state& lua = *luaState_;
    sol::table lifecycle = lua["lifecycle"];
    if (!lifecycle.valid()) return 0;

    sol::table spawnBag   = lifecycle["_onSpawn"];
    sol::table destroyBag = lifecycle["_onDestroy"];

    uint32_t dispatched = 0;
    for (const LifecycleEvent& ev : events) {
        sol::object listObj = (ev.kind == LifecycleEvent::Kind::Spawned
                               ? spawnBag : destroyBag)[ev.className];
        if (!listObj.is<sol::table>()) continue;
        sol::table list = listObj.as<sol::table>();

        sol::table tagsTbl = lua.create_table();
        int ti = 1;
        for (const std::string& t : ev.tags)
            tagsTbl[ti++] = t;

        // ipairs-style walk — Lua arrays are 1-indexed.
        for (size_t i = 1; ; ++i) {
            sol::object slot = list[i];
            if (!slot.valid() || slot == sol::nil) break;
            sol::protected_function fn = slot.as<sol::protected_function>();
            if (!fn.valid()) continue;
            auto result = fn(ev.id, tagsTbl);
            ++dispatched;
            if (!result.valid()) {
                sol::error err = result;
                sol::protected_function debugLog = lua["debug"]["log"];
                if (debugLog.valid())
                    debugLog(std::string("[lifecycle handler error] ")
                             + err.what());
            }
        }
    }
    return dispatched;
}

} // namespace ArtCade::Modules
