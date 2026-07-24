-- =============================================================================
-- PauseManager — Logic Component
-- Manages a ref-counted pause stack (multiple simultaneous sources).
-- Sets time.setScale(0) when paused, 1 when fully resumed.
-- Emits "pause.paused" / "pause.resumed" via EventBus.
--
-- Usage:
--   local p = Pause.new()
--   p:request("menu")        -- freezes game time
--   p:release("menu")        -- resumes if no other source is active
--   p:toggle("cutscene")     -- request/release depending on current state
--   p:isPaused()             -- bool
-- =============================================================================

Pause = {}
Pause.__index = Pause

function Pause.new()
    local self = setmetatable({}, Pause)
    self._sources = {}   -- active pause sources (table used as set)
    self._paused  = false
    return self
end

-- Register a pause source.
function Pause:request(source)
    self._sources[source] = true
    self:_sync()
end

-- Release a pause source.
function Pause:release(source)
    self._sources[source] = nil
    self:_sync()
end

-- Toggle: request if not active, release if already active.
function Pause:toggle(source)
    if self._sources[source] then
        self:release(source)
    else
        self:request(source)
    end
end

-- Returns true if at least one source is holding a pause.
function Pause:isPaused()
    return self._paused
end

-- List active pause sources (for debugging).
function Pause:activeSources()
    local list = {}
    for src in pairs(self._sources) do
        list[#list + 1] = src
    end
    return list
end

-- Internal: reconcile state and fire side-effects.
function Pause:_sync()
    local any = false
    for _ in pairs(self._sources) do any = true; break end

    local changed = (any ~= self._paused)
    self._paused  = any

    -- Freeze / unfreeze game time
    if time and time.setScale then
        time.setScale(any and 0 or 1)
    end

    -- Notify via EventBus
    if changed and event and event.emit then
        event.emit(any and "pause.paused" or "pause.resumed", {
            sources = self:activeSources()
        })
    end
end
