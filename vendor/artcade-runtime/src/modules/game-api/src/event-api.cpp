// event-api.cpp — Lua-side EventBus
//
// Exposes:
//   event.on(name, callback)    → unsubscribe handle (function)
//   event.once(name, callback)  → fires once then auto-removes
//   event.off(name, callback)   → remove a specific listener
//   event.emit(name, payload?)  → call all listeners for 'name'
//
// Lua listeners keep payload support locally. Event names are mirrored to the
// C++ EventBus so runtime systems such as DialogManager can participate.

#include "../include/game-api.h"
#include "../../event-bus/include/event-bus.h"
#include <sol/sol.hpp>
#include <iostream>

namespace ArtCade::Modules {

void GameAPI::bindEventAPI(sol::state& lua) {
    if (ctx_.eventBus) {
        for (const uint32_t token : eventBridgeTokens_)
            ctx_.eventBus->unsubscribe(token);
    }
    eventBridgeTokens_.clear();
    luaState_ = &lua;

    lua.set_function("_artcade_event_subscribe_native",
        [this](const std::string& name) -> bool {
            if (!ctx_.eventBus || name.empty()) return false;
            const uint32_t token = ctx_.eventBus->subscribe(
                name,
                [this, name](const std::any&) {
                    if (!luaState_) return;
                    sol::protected_function dispatch =
                        (*luaState_)["_artcade_dispatch_native_event"];
                    if (!dispatch.valid()) return;
                    sol::protected_function_result result = dispatch(name);
                    if (!result.valid()) {
                        sol::error err = result;
                        std::cerr << "[Event] native dispatch failed: "
                                  << err.what() << "\n";
                    }
                });
            eventBridgeTokens_.push_back(token);
            return true;
        });

    lua.set_function("_artcade_event_emit_native",
        [this](const std::string& name) {
            if (ctx_.eventBus && !name.empty())
                ctx_.eventBus->emit(name);
        });

    lua.script(R"(
        local _listeners = {}
        local _native_subscribed = {}
        local _emitting_to_native = {}

        event = {}

        local function dispatch_local(name, payload)
            local list = _listeners[name]
            if not list then return end
            local snapshot = {}
            for _, e in ipairs(list) do snapshot[#snapshot+1] = e end
            for _, entry in ipairs(snapshot) do
                entry.cb(payload)
            end
            local newList = {}
            for _, e in ipairs(list) do
                if not e.once then newList[#newList+1] = e end
            end
            _listeners[name] = newList
        end

        function _artcade_dispatch_native_event(name)
            if _emitting_to_native[name] then return end
            dispatch_local(name, nil)
        end

        local function ensure_native_subscription(name)
            if _native_subscribed[name] then return end
            if _artcade_event_subscribe_native(name) then
                _native_subscribed[name] = true
            end
        end

        -- Register a persistent listener.
        -- Returns an unsubscribe function: call it to remove this listener.
        function event.on(name, cb)
            if not _listeners[name] then _listeners[name] = {} end
            ensure_native_subscription(name)
            local entry = { cb = cb, once = false }
            table.insert(_listeners[name], entry)
            return function()
                local list = _listeners[name]
                if not list then return end
                for i = #list, 1, -1 do
                    if list[i] == entry then table.remove(list, i) end
                end
            end
        end

        -- Register a one-shot listener (auto-removed after first fire).
        function event.once(name, cb)
            if not _listeners[name] then _listeners[name] = {} end
            ensure_native_subscription(name)
            table.insert(_listeners[name], { cb = cb, once = true })
        end

        -- Manually remove all copies of cb from name.
        function event.off(name, cb)
            local list = _listeners[name]
            if not list then return end
            for i = #list, 1, -1 do
                if list[i].cb == cb then table.remove(list, i) end
            end
        end

        -- Emit name with optional payload; calls all registered listeners.
        function event.emit(name, payload)
            dispatch_local(name, payload)
            _emitting_to_native[name] = true
            _artcade_event_emit_native(name)
            _emitting_to_native[name] = nil
        end
    )");
}

} // namespace ArtCade::Modules
