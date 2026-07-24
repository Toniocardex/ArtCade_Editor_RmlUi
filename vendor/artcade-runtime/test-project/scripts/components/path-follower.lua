-- =============================================================================
-- PathFollower — Logic Component
-- Moves an entity smoothly along a sequence of waypoints.
--
-- Usage:
--   local pf = PathFollower.new(entityId, {
--       waypoints = { {x=100,y=200}, {x=400,y=200}, {x=400,y=500} },
--       speed     = 80,
--       mode      = "loop"    -- "loop" | "pingpong" | "once"
--   })
--   pf:update(dt)   -- call every tick
--   pf:pause() / pf:resume()
--   pf:isDone()     -- true when mode="once" and last waypoint reached
--   pf:setTarget(index)  -- jump to waypoint index
-- =============================================================================

PathFollower = {}
PathFollower.__index = PathFollower

function PathFollower.new(entityId, cfg)
    cfg = cfg or {}
    local self = setmetatable({}, PathFollower)
    self.id        = entityId
    self.waypoints = cfg.waypoints or {}
    self.speed     = cfg.speed    or 80
    self.mode      = cfg.mode     or "loop"   -- "loop" | "pingpong" | "once"
    self._current  = 1
    self._dir      = 1    -- +1 or -1 for pingpong
    self._paused   = false
    self._done     = false
    return self
end

-- Advance the follower. Must be called each game tick.
function PathFollower:update(dt)
    if self._paused or self._done then return end
    if #self.waypoints == 0 then return end

    local wp    = self.waypoints[self._current]
    local px, py = entity.position(self.id)
    local dx    = wp.x - px
    local dy    = wp.y - py
    local dist  = math.sqrt(dx * dx + dy * dy)

    if dist < 2 then
        -- Reached waypoint — advance
        self:_advance()
    else
        -- Move toward waypoint
        local nx = dx / dist
        local ny = dy / dist
        entity.setPosition(self.id,
            px + nx * self.speed * dt,
            py + ny * self.speed * dt)
    end
end

-- Pause / resume movement.
function PathFollower:pause()  self._paused = true  end
function PathFollower:resume() self._paused = false end

-- Returns true when mode="once" and the last waypoint has been reached.
function PathFollower:isDone() return self._done end

-- Jump to a specific waypoint index immediately.
function PathFollower:setTarget(index)
    self._current = math.max(1, math.min(index, #self.waypoints))
    self._done    = false
end

-- Current waypoint index.
function PathFollower:currentIndex() return self._current end

-- Internal: select next waypoint according to mode.
function PathFollower:_advance()
    local n = #self.waypoints

    if self.mode == "loop" then
        self._current = (self._current % n) + 1

    elseif self.mode == "pingpong" then
        self._current = self._current + self._dir
        if self._current > n then
            self._dir    = -1
            self._current = n - 1
        elseif self._current < 1 then
            self._dir    =  1
            self._current = 2
        end

    elseif self.mode == "once" then
        if self._current < n then
            self._current = self._current + 1
        else
            self._done = true
            if event and event.emit then
                event.emit("pathfollower.done", { id = self.id })
            end
        end
    end
end
