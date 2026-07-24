-- =============================================================================
-- Platformer input helper (optional)
--
-- Prefer the editor component **platformerController** (C++ tick) for physics,
-- coyote time, and jump buffer. Use this script only when you want default
-- WASD + Space wiring without Logic Board.
--
-- Do NOT call :update() on the same entity that has platformerController.
-- =============================================================================

Platformer = {}
Platformer.__index = Platformer

function Platformer.new(entityId, cfg)
    cfg = cfg or {}
    local self = setmetatable({}, Platformer)
    self.id      = entityId
    self._enabled = true
    self._moveKeys = {}

    local jumpCodes = { "Space", "KeyW", "ArrowUp" }
    for _, code in ipairs(jumpCodes) do
        input.onPressed(code, function()
            platformer.requestJump(self.id)
        end)
    end

    local moveCodes = { "KeyA", "ArrowLeft", "KeyD", "ArrowRight" }
    for _, code in ipairs(moveCodes) do
        self._moveKeys[code] = input.isKeyDown(code)
        input.onPressed(code, function() self._moveKeys[code] = true end)
        input.onReleased(code, function() self._moveKeys[code] = false end)
    end

    return self
end

function Platformer:update(_dt)
    if not self._enabled then return end

    local dx = 0
    if self._moveKeys["KeyA"] or self._moveKeys["ArrowLeft"]  then dx = dx - 1 end
    if self._moveKeys["KeyD"] or self._moveKeys["ArrowRight"] then dx = dx + 1 end

    if dx == 0 then
        movement.clearIntent(self.id)
    else
        movement.setIntent(self.id, dx, 0)
    end
end

function Platformer:isGrounded()
    return platformer.isGrounded(self.id)
end

function Platformer:setEnabled(flag)
    self._enabled = flag
    if not flag then
        movement.clearIntent(self.id)
    end
end
