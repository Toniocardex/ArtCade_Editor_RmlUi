// time-api.cpp — Lua-side Time API
//
// Exposes:
//   time.now()                   → float  — total elapsed game time in seconds
//   time.delay(seconds, cb)      → fires cb once after 'seconds'
//   time.every(interval, cb)     → fires cb every 'interval' seconds;
//                                   returns a cancel function
//   time.scale                   → float  — current time scale (default 1.0)
//
// Implementation:
//   A global Lua function _time_update(dt) is injected.
//   LuaHost::tick() calls it before the user's tick() each frame.
//   This advances _gameTime and fires pending timers.

#include "../include/game-api.h"
#include <sol/sol.hpp>

namespace ArtCade::Modules {

void GameAPI::bindTimeAPI(sol::state& lua) {
    lua.script(R"(
        local _gameTime  = 0.0
        local _timers    = {}
        local _scale     = 1.0
        local _paused    = false
        local _prevScale = 1.0   -- scale to restore on resume

        time = {}

        -- Total elapsed game time in seconds (respects time.scale).
        function time.now()
            return _gameTime
        end

        -- Fire cb once after 'seconds' of game time.
        function time.delay(seconds, cb)
            table.insert(_timers, {
                remaining = seconds,
                interval  = nil,
                cb        = cb,
                once      = true
            })
        end

        -- Event-driven alias used by Logic Board intent scripts.
        function time.after(seconds, cb)
            return time.delay(seconds, cb)
        end

        -- Fire cb every 'interval' seconds. Returns a cancel function.
        function time.every(interval, cb)
            local entry = {
                remaining = interval,
                interval  = interval,
                cb        = cb,
                once      = false
            }
            table.insert(_timers, entry)
            return function()
                for i = #_timers, 1, -1 do
                    if _timers[i] == entry then
                        table.remove(_timers, i)
                        return
                    end
                end
            end
        end

        -- Read/write time scale (1.0 = normal, 0.5 = half-speed, 0 = frozen).
        function time.setScale(s)
            _scale = (s ~= nil and s >= 0) and s or 1.0
            _paused = (_scale == 0)
        end
        function time.getScale()
            return _scale
        end

        -- Pause freezes gameplay time (timers, movement integration) while the
        -- frame still renders and input is still polled, so an unpause rule can
        -- run. resume() restores the pre-pause scale (slow-mo survives a pause).
        function time.pause()
            if not _paused then
                if _scale > 0 then _prevScale = _scale end
                _scale = 0
                _paused = true
            end
        end
        function time.resume()
            if _paused then
                _scale = (_prevScale > 0) and _prevScale or 1.0
                _paused = false
            end
        end
        function time.togglePause()
            if _paused then time.resume() else time.pause() end
        end
        function time.isPaused()
            return _paused
        end

        -- Called by LuaHost::tick() before the user's tick(dt).
        -- dt is real delta time; scale it for game timers.
        function _time_update(dt)
            local scaledDt = dt * _scale
            _gameTime = _gameTime + scaledDt

            local toRemove = {}
            for i, timer in ipairs(_timers) do
                timer.remaining = timer.remaining - scaledDt
                if timer.remaining <= 0 then
                    -- Use protected call so a bad callback doesn't kill the loop
                    local ok, err = pcall(timer.cb)
                    if not ok then
                        debug.log("[time] timer callback error: " .. tostring(err))
                    end
                    if timer.once then
                        table.insert(toRemove, i)
                    else
                        timer.remaining = timer.remaining + timer.interval
                    end
                end
            end
            for i = #toRemove, 1, -1 do
                table.remove(_timers, toRemove[i])
            end
        end
    )");
}

} // namespace ArtCade::Modules
