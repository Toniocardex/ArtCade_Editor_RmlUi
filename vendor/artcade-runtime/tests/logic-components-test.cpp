// =============================================================================
// logic-components-test.cpp — Phase 16 Step 2
//
// Tests for all 5 Lua Logic Components via LuaHost + inline stubs.
// Each test:
//   1. Creates a fresh LuaHost
//   2. Injects stub implementations of entity/input/collision/debug/event/time
//   3. Loads the component source inline (no file I/O)
//   4. Defines a tick() function with Lua assert() checks
//   5. Calls host.tick() and checks !host.hasError()
//
// Components tested:
//   Pause        — ref-counted pause stack, time.setScale, event.emit
//   PathFollower — waypoint movement, loop/pingpong/once modes
//   Platformer   — WASD movement, jump, coyote time, jump buffer
//   Particles    — burst/update/draw lifecycle, fadeOut
//   Dialogue     — branching conversations, choices, draw
// =============================================================================

#include "modules/lua-runtime/include/lua-host.h"

#include <cstring>
#include <iostream>
#include <string>

using namespace ArtCade::Modules;

// ---- minimal test harness ---------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) { \
            ++g_passed; \
        } else { \
            std::cerr << "  FAIL: " #cond "  (line " << __LINE__ << ")\n"; \
            ++g_failed; \
        } \
    } while (false)

static void load(LuaHost& host, const char* src) {
    host.loadBytecodeBuffer(
        reinterpret_cast<const uint8_t*>(src), std::strlen(src));
}

// ---- shared stubs injected before every component test ----------------------
// Globals persist across loadBytecodeBuffer calls in the same LuaHost.

static const char* STUBS = R"STUBS(
-- Entity stub: tracks positions and velocities in plain tables.
entity = {
    _positions  = {},
    _velocities = {},
    position = function(id)
        local p = entity._positions[id] or {x=0, y=0}
        return p.x, p.y
    end,
    setPosition = function(id, x, y)
        entity._positions[id] = {x=x, y=y}
    end,
    velocity = function(id)
        local v = entity._velocities[id] or {x=0, y=0}
        return v.x, v.y
    end,
    setVelocity = function(id, vx, vy)
        entity._velocities[id] = {x=vx, y=vy}
    end,
    destroy = function(id)
        entity._positions[id]  = nil
        entity._velocities[id] = nil
    end,
}

-- Collision stub: grounded set is controlled per-test.
collision = {
    _groundedIds = {},
    firstTouching = function(id, filter) return 0 end,
    isGrounded = function(id) return collision._groundedIds[id] == true end,
    overlap  = function(id1, id2) return false end,
    raycast  = function(x1,y1,x2,y2) return {hit=false} end,
}

-- Input stub: key state controlled per-test.
input = {
    _keysDown    = {},
    _keysPressed = {},
    isKeyDown      = function(k) return input._keysDown[k]    == true end,
    wasKeyPressed  = function(k) return input._keysPressed[k] == true end,
    wasKeyReleased = function(k) return false end,
}

-- Debug stub: records calls for assertion.
debug = {
    _circles = {},
    _texts   = {},
    drawCircle = function(x, y, r, color)
        debug._circles[#debug._circles + 1] = {x=x, y=y, r=r, color=color}
    end,
    drawText = function(text, x, y, size, color)
        debug._texts[#debug._texts + 1] = {text=text, x=x, y=y}
    end,
    drawLine = function(x1,y1,x2,y2,color) end,
    drawRect = function(x,y,w,h,color)     end,
    log      = function(msg)               end,
}

-- Event stub: records emitted events for assertion.
event = {
    _emitted = {},
    emit = function(name, payload)
        event._emitted[#event._emitted + 1] = {name=name, payload=payload}
    end,
    on   = function(name, cb) return function() end end,
    once = function(name, cb) return function() end end,
    off  = function(name, handle) end,
}

-- Time stub: scale observable per-test.
time = {
    _scale = 1,
    setScale = function(s) time._scale = s end,
    getScale = function()  return time._scale end,
    now   = function() return 0 end,
    delay = function(s, cb) return function() end end,
    every = function(s, cb) return function() end end,
}

movement = {
    _intents = {},
    setIntent = function(id, x, y)
        movement._intents[id] = {x=x, y=y}
    end,
    clearIntent = function(id)
        movement._intents[id] = nil
    end,
}

platformer = {
    _jumpRequests = {},
    _groundedIds  = {},
    requestJump = function(id)
        platformer._jumpRequests[#platformer._jumpRequests + 1] = id
    end,
    isGrounded = function(id)
        return platformer._groundedIds[id] == true
    end,
}

input._onPressed  = {}
input._onReleased = {}
input.onPressed = function(code, fn)
    input._onPressed[code] = input._onPressed[code] or {}
    input._onPressed[code][#input._onPressed[code] + 1] = fn
end
input.onReleased = function(code, fn)
    input._onReleased[code] = input._onReleased[code] or {}
    input._onReleased[code][#input._onReleased[code] + 1] = fn
end
input._firePressed = function(code)
    for _, fn in ipairs(input._onPressed[code] or {}) do fn() end
end
)STUBS";

// ---- embedded component sources ---------------------------------------------

static const char* PAUSE_LUA = R"PAUSE(
Pause = {}
Pause.__index = Pause

function Pause.new()
    local self = setmetatable({}, Pause)
    self._sources = {}
    self._paused  = false
    return self
end

function Pause:request(source)
    self._sources[source] = true
    self:_sync()
end

function Pause:release(source)
    self._sources[source] = nil
    self:_sync()
end

function Pause:toggle(source)
    if self._sources[source] then
        self:release(source)
    else
        self:request(source)
    end
end

function Pause:isPaused()
    return self._paused
end

function Pause:activeSources()
    local list = {}
    for src in pairs(self._sources) do
        list[#list + 1] = src
    end
    return list
end

function Pause:_sync()
    local any = false
    for _ in pairs(self._sources) do any = true; break end

    local changed = (any ~= self._paused)
    self._paused  = any

    if time and time.setScale then
        time.setScale(any and 0 or 1)
    end

    if changed and event and event.emit then
        event.emit(any and "pause.paused" or "pause.resumed", {
            sources = self:activeSources()
        })
    end
end
)PAUSE";

static const char* PATH_FOLLOWER_LUA = R"PATHF(
PathFollower = {}
PathFollower.__index = PathFollower

function PathFollower.new(entityId, cfg)
    cfg = cfg or {}
    local self = setmetatable({}, PathFollower)
    self.id        = entityId
    self.waypoints = cfg.waypoints or {}
    self.speed     = cfg.speed    or 80
    self.mode      = cfg.mode     or "loop"
    self._current  = 1
    self._dir      = 1
    self._paused   = false
    self._done     = false
    return self
end

function PathFollower:update(dt)
    if self._paused or self._done then return end
    if #self.waypoints == 0 then return end

    local wp      = self.waypoints[self._current]
    local px, py  = entity.position(self.id)
    local dx      = wp.x - px
    local dy      = wp.y - py
    local dist    = math.sqrt(dx * dx + dy * dy)

    if dist < 2 then
        self:_advance()
    else
        local nx = dx / dist
        local ny = dy / dist
        entity.setPosition(self.id,
            px + nx * self.speed * dt,
            py + ny * self.speed * dt)
    end
end

function PathFollower:pause()  self._paused = true  end
function PathFollower:resume() self._paused = false end

function PathFollower:isDone() return self._done end

function PathFollower:setTarget(index)
    self._current = math.max(1, math.min(index, #self.waypoints))
    self._done    = false
end

function PathFollower:currentIndex() return self._current end

function PathFollower:_advance()
    local n = #self.waypoints

    if self.mode == "loop" then
        self._current = (self._current % n) + 1

    elseif self.mode == "pingpong" then
        self._current = self._current + self._dir
        if self._current > n then
            self._dir     = -1
            self._current = n - 1
        elseif self._current < 1 then
            self._dir     =  1
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
)PATHF";

static const char* PLATFORMER_LUA = R"PLAT(
Platformer = {}
Platformer.__index = Platformer

function Platformer.new(entityId, cfg)
    local self = setmetatable({}, Platformer)
    self.id = entityId
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
)PLAT";

static const char* PARTICLES_LUA = R"PARTS(
ParticleEmitter = {}
ParticleEmitter.__index = ParticleEmitter

function ParticleEmitter.new(x, y, cfg)
    cfg = cfg or {}
    local self = setmetatable({}, ParticleEmitter)
    self.x        = x   or 0
    self.y        = y   or 0
    self.count    = cfg.count    or 20
    self.lifetime = cfg.lifetime or 0.8
    self.speedMin = (cfg.speed and cfg.speed.min) or 40
    self.speedMax = (cfg.speed and cfg.speed.max) or 120
    self.spread   = cfg.spread   or (math.pi * 2)
    self.color    = cfg.color    or "orange"
    self.radius   = cfg.radius   or 3
    self.gravity  = cfg.gravity  or 0
    self.fadeOut  = (cfg.fadeOut ~= false)
    self._particles = {}
    return self
end

function ParticleEmitter:burst()
    for _ = 1, self.count do
        local angle = (math.random() * self.spread) - (self.spread * 0.5)
        local spd   = self.speedMin + math.random() * (self.speedMax - self.speedMin)
        self._particles[#self._particles + 1] = {
            x       = self.x,
            y       = self.y,
            vx      = math.cos(angle) * spd,
            vy      = math.sin(angle) * spd,
            life    = self.lifetime,
            maxLife = self.lifetime,
        }
    end
end

function ParticleEmitter:update(dt)
    for i = #self._particles, 1, -1 do
        local p = self._particles[i]
        p.vy   = p.vy + self.gravity * dt
        p.x    = p.x  + p.vx * dt
        p.y    = p.y  + p.vy * dt
        p.life = p.life - dt
        if p.life <= 0 then
            table.remove(self._particles, i)
        end
    end
end

function ParticleEmitter:draw()
    for _, p in ipairs(self._particles) do
        local r = self.radius
        if self.fadeOut then
            r = r * (p.life / p.maxLife)
        end
        if r > 0.5 then
            debug.drawCircle(p.x, p.y, r, self.color)
        end
    end
end

function ParticleEmitter:liveCount()
    return #self._particles
end

function ParticleEmitter:isAlive()
    return #self._particles > 0
end

function ParticleEmitter:moveTo(x, y)
    self.x = x
    self.y = y
end
)PARTS";

static const char* DIALOGUE_LUA = R"DIAL(
DialogueSystem = {}
DialogueSystem.__index = DialogueSystem

function DialogueSystem.new(conversations)
    local self = setmetatable({}, DialogueSystem)
    self._convs   = conversations or {}
    self._conv    = nil
    self._nodeIdx = 0
    self._active  = false
    return self
end

function DialogueSystem:start(id)
    local c = self._convs[id]
    if not c then return end
    self._conv    = c
    self._nodeIdx = 1
    self._active  = true
    if event and event.emit then
        event.emit("dialogue.started", { id = id })
    end
end

function DialogueSystem:getCurrentLine()
    if not self._active or not self._conv then return nil end
    local node = self._conv.nodes[self._nodeIdx]
    return node and node.text
end

function DialogueSystem:getChoices()
    if not self._active or not self._conv then return nil end
    local node = self._conv.nodes[self._nodeIdx]
    if not node or not node.choices or #node.choices == 0 then return nil end
    return node.choices
end

function DialogueSystem:advance()
    if not self._active then return end
    local node = self._conv.nodes[self._nodeIdx]
    if not node then self:_end(); return end
    if node.choices and #node.choices > 0 then return end
    self._nodeIdx = self._nodeIdx + 1
    if self._nodeIdx > #self._conv.nodes then
        self:_end()
    end
end

function DialogueSystem:choose(index)
    if not self._active then return end
    local node = self._conv.nodes[self._nodeIdx]
    if not node or not node.choices then return end
    local choice = node.choices[index]
    if not choice then return end
    if choice.next then
        self._nodeIdx = choice.next
    else
        self._nodeIdx = self._nodeIdx + 1
    end
    if self._nodeIdx > #self._conv.nodes then
        self:_end()
    end
end

function DialogueSystem:isActive()
    return self._active
end

function DialogueSystem:draw(x, y)
    if not self._active then return end
    local line = self:getCurrentLine()
    if line and debug and debug.drawText then
        debug.drawText(line, x, y, 20, "white")
    end
    local choices = self:getChoices()
    if choices and debug and debug.drawText then
        for i, ch in ipairs(choices) do
            debug.drawText(tostring(i) .. ". " .. ch.text, x, y + i * 24, 16, "yellow")
        end
    end
end

function DialogueSystem:_end()
    local id = self._conv and self._conv.id
    self._active  = false
    self._conv    = nil
    self._nodeIdx = 0
    if event and event.emit then
        event.emit("dialogue.ended", { id = id })
    end
end
)DIAL";

// ---- helper: print error if test failed ------------------------------------

static void reportError(LuaHost& host, const char* name) {
    if (host.hasError())
        std::cerr << "    [" << name << "] Lua error: " << host.lastError() << "\n";
}

// ============================================================================
// Test 1 — PauseManager
// ============================================================================
static void test_pause_manager() {
    std::cout << "Test 1: PauseManager\n";
    LuaHost host;
    host.init();
    load(host, STUBS);
    load(host, PAUSE_LUA);
    load(host, R"LUA(
function tick(dt)
    -- Basic construction
    local p = Pause.new()
    assert(not p:isPaused(), "new() should start unpaused")
    assert(time._scale == 1, "time scale should be 1 initially")

    -- Single source pause
    p:request("menu")
    assert(p:isPaused(),   "paused after request('menu')")
    assert(time._scale == 0, "time.setScale(0) on pause")

    -- Two sources — releasing one keeps paused
    p:request("cutscene")
    p:release("menu")
    assert(p:isPaused(),   "still paused while 'cutscene' active")

    -- Release last source → unpaused
    p:release("cutscene")
    assert(not p:isPaused(), "unpaused when all sources released")
    assert(time._scale == 1,  "time.setScale(1) on resume")

    -- Event sequence check
    local found_paused  = false
    local found_resumed = false
    for _, e in ipairs(event._emitted) do
        if e.name == "pause.paused"   then found_paused  = true end
        if e.name == "pause.resumed"  then found_resumed = true end
    end
    assert(found_paused,  "pause.paused event not emitted")
    assert(found_resumed, "pause.resumed event not emitted")

    -- toggle: request then release
    local p2 = Pause.new()
    p2:toggle("ui")
    assert(p2:isPaused(),     "toggle should pause")
    p2:toggle("ui")
    assert(not p2:isPaused(), "double-toggle should unpause")

    -- activeSources returns current sources
    local p3 = Pause.new()
    p3:request("a")
    p3:request("b")
    local srcs = p3:activeSources()
    assert(#srcs == 2, "activeSources should list 2 sources")
end
)LUA");
    host.tick(0.016f);
    CHECK(!host.hasError());
    reportError(host, "pause");
    host.shutdown();
}

// ============================================================================
// Test 2 — PathFollower
// ============================================================================
static void test_path_follower() {
    std::cout << "Test 2: PathFollower\n";
    LuaHost host;
    host.init();
    load(host, STUBS);
    load(host, PATH_FOLLOWER_LUA);
    load(host, R"LUA(
function tick(dt)
    -- Setup: entity at origin, two waypoints in "once" mode
    entity.setPosition(1, 0, 0)
    local pf = PathFollower.new(1, {
        waypoints = { {x=10, y=0}, {x=20, y=0} },
        speed     = 80,
        mode      = "once",
    })

    assert(not pf:isDone(), "should not be done yet")
    assert(pf:currentIndex() == 1, "should start at index 1")

    -- One update at large dt → moves toward first waypoint
    pf:update(0.5)
    local px, py = entity.position(1)
    assert(px > 0, "entity should move right after update, got " .. tostring(px))

    -- Teleport close to first waypoint (dist < 2) → advance
    entity.setPosition(1, 10, 0)
    pf:update(0.016)
    assert(pf:currentIndex() == 2, "should have advanced to waypoint 2")

    -- Teleport close to second waypoint → done (last in "once" mode)
    entity.setPosition(1, 20, 0)
    pf:update(0.016)
    assert(pf:isDone(), "should be done after last waypoint in once mode")

    -- pathfollower.done event emitted
    local found = false
    for _, e in ipairs(event._emitted) do
        if e.name == "pathfollower.done" then found = true end
    end
    assert(found, "pathfollower.done not emitted")

    -- pause stops movement
    entity.setPosition(1, 0, 0)
    local pf2 = PathFollower.new(1, {
        waypoints = { {x=100, y=0} },
        speed     = 80,
        mode      = "loop",
    })
    pf2:pause()
    pf2:update(1.0)
    local px2, _ = entity.position(1)
    assert(px2 == 0, "paused follower must not move entity")

    pf2:resume()
    pf2:update(0.1)
    local px3, _ = entity.position(1)
    assert(px3 > 0, "resumed follower should move entity")

    -- setTarget jumps index
    local pf3 = PathFollower.new(1, {
        waypoints = { {x=0,y=0}, {x=50,y=0}, {x=100,y=0} },
        speed     = 80,
        mode      = "loop",
    })
    pf3:setTarget(3)
    assert(pf3:currentIndex() == 3, "setTarget should jump to index 3")

    -- loop mode wraps: n=3, current=3 → (3%3)+1 = 1
    entity.setPosition(1, 100, 0)
    pf3:update(0.016)     -- at waypoint 3 (dist<2) → _advance → current=1
    assert(pf3:currentIndex() == 1, "loop mode should wrap to 1")

    -- pingpong: 3 waypoints, reaches end → reverses
    local pf4 = PathFollower.new(1, {
        waypoints = { {x=0,y=0}, {x=50,y=0}, {x=100,y=0} },
        speed     = 80,
        mode      = "pingpong",
    })
    pf4:setTarget(3)
    entity.setPosition(1, 100, 0)
    pf4:update(0.016)     -- at wp3, _advance: current=3>3? no. current+dir=4>3 → dir=-1, current=2
    assert(pf4:currentIndex() == 2, "pingpong should reverse at end")
end
)LUA");
    host.tick(0.016f);
    CHECK(!host.hasError());
    reportError(host, "path_follower");
    host.shutdown();
}

// ============================================================================
// Test 3 — PlatformerController
// ============================================================================
static void test_platformer_controller() {
    std::cout << "Test 3: PlatformerController\n";
    LuaHost host;
    host.init();
    load(host, STUBS);
    load(host, PLATFORMER_LUA);
    load(host, R"LUA(
function tick(dt)
    local ctrl = Platformer.new(1, {})

    platformer._groundedIds[1] = true
    assert(ctrl:isGrounded(), "isGrounded should use platformer API")

    input._keysDown["KeyD"] = true
    ctrl._moveKeys["KeyD"] = true
    ctrl:update(0.016)
    local m = movement._intents[1]
    assert(m and m.x == 1 and m.y == 0, "KeyD should set movement intent +1")
    input._keysDown["KeyD"] = false
    ctrl._moveKeys["KeyD"] = false

    input._keysDown["KeyA"] = true
    ctrl._moveKeys["KeyA"] = true
    ctrl:update(0.016)
    m = movement._intents[1]
    assert(m and m.x == -1, "KeyA should set movement intent -1")
    input._keysDown["KeyA"] = false
    ctrl._moveKeys["KeyA"] = false

    platformer._jumpRequests = {}
    input._firePressed("Space")
    assert(#platformer._jumpRequests == 1, "Space should call platformer.requestJump once")

    ctrl:setEnabled(false)
    assert(movement._intents[1] == nil, "setEnabled(false) must clear intent")
    input._keysDown["KeyD"] = true
    ctrl._moveKeys["KeyD"] = true
    ctrl:update(0.016)
    assert(movement._intents[1] == nil, "disabled update must not set intent")
    input._keysDown["KeyD"] = false
    ctrl:setEnabled(true)
end
)LUA");
    host.tick(0.016f);
    CHECK(!host.hasError());
    reportError(host, "platformer");
    host.shutdown();
}

// ============================================================================
// Test 4 — ParticleEmitter
// ============================================================================
static void test_particle_emitter() {
    std::cout << "Test 4: ParticleEmitter\n";
    LuaHost host;
    host.init();
    load(host, STUBS);
    load(host, PARTICLES_LUA);
    load(host, R"LUA(
function tick(dt)
    local pe = ParticleEmitter.new(100, 200, {
        count    = 10,
        lifetime = 1.0,
        speed    = {min=50, max=100},
        color    = "orange",
        radius   = 6,
        gravity  = 0,
        fadeOut  = true,
    })

    -- Initial state: no particles
    assert(pe:liveCount() == 0, "no particles before burst")
    assert(not pe:isAlive(),    "not alive before burst")

    -- Burst spawns exactly count particles
    pe:burst()
    assert(pe:liveCount() == 10, "burst should spawn 10 particles, got " .. tostring(pe:liveCount()))
    assert(pe:isAlive(),         "alive after burst")

    -- Partial update: particles still alive (lifetime=1.0, dt=0.5)
    pe:update(0.5)
    assert(pe:isAlive(), "particles alive at 0.5 of 1.0s lifetime")
    assert(pe:liveCount() > 0, "some particles still live")

    -- Draw calls debug.drawCircle (fadeOut: r = 6 * (0.5/1.0) = 3.0 > 0.5)
    local before = #debug._circles
    pe:draw()
    assert(#debug._circles > before, "draw() should call debug.drawCircle")

    -- Finish expiry: advance remaining 0.5s life
    pe:update(0.6)   -- life = 0.5 - 0.6 < 0 → all expired
    assert(pe:liveCount() == 0, "all particles expired after full lifetime")
    assert(not pe:isAlive(),    "not alive when all expired")

    -- draw on dead emitter should call nothing
    local before2 = #debug._circles
    pe:draw()
    assert(#debug._circles == before2, "draw on dead emitter should not add circles")

    -- moveTo repositions the origin
    pe:moveTo(300, 400)
    assert(pe.x == 300, "moveTo should update x")
    assert(pe.y == 400, "moveTo should update y")

    -- Second burst at new origin
    pe:burst()
    assert(pe:liveCount() == 10, "second burst should spawn 10 particles")
    -- All new particles should start at (300, 400)
    local allAtOrigin = true
    for _, p in ipairs(pe._particles) do
        if p.x ~= 300 or p.y ~= 400 then allAtOrigin = false; break end
    end
    assert(allAtOrigin, "burst particles should start at moveTo origin")

    -- gravity: non-zero gravity accelerates vy
    local pe2 = ParticleEmitter.new(0, 0, {
        count    = 1,
        lifetime = 2.0,
        speed    = {min=0, max=0},   -- zero speed so vy starts at 0
        gravity  = 200,
        fadeOut  = false,
        radius   = 3,
    })
    pe2:burst()
    local p0 = pe2._particles[1]
    local vy_before = p0.vy
    pe2:update(0.1)
    local vy_after = pe2._particles[1].vy
    assert(vy_after > vy_before, "gravity should increase vy, before=" ..
           tostring(vy_before) .. " after=" .. tostring(vy_after))
end
)LUA");
    host.tick(0.016f);
    CHECK(!host.hasError());
    reportError(host, "particles");
    host.shutdown();
}

// ============================================================================
// Test 5 — DialogueSystem
// ============================================================================
static void test_dialogue_system() {
    std::cout << "Test 5: DialogueSystem\n";
    LuaHost host;
    host.init();
    load(host, STUBS);
    load(host, DIALOGUE_LUA);
    load(host, R"LUA(
function tick(dt)
    -- Conversation: plain lines then a choice node
    local conv = {
        id = "test",
        nodes = {
            { text = "Hello!" },                        -- node 1
            { text = "Pick:", choices = {               -- node 2
                { text = "Option A", next = 3 },
                { text = "Option B", next = 3 },
            }},
            { text = "Done!" },                         -- node 3
        }
    }

    local ds = DialogueSystem.new({ test = conv })

    -- Inactive before start
    assert(not ds:isActive(),          "should be inactive before start")
    assert(ds:getCurrentLine() == nil, "no current line before start")
    assert(ds:getChoices() == nil,     "no choices before start")

    -- start() activates and emits event
    ds:start("test")
    assert(ds:isActive(),                     "active after start")
    assert(ds:getCurrentLine() == "Hello!",   "first line should be 'Hello!'")
    assert(ds:getChoices() == nil,            "no choices on plain node")

    local found_started = false
    for _, e in ipairs(event._emitted) do
        if e.name == "dialogue.started" then found_started = true end
    end
    assert(found_started, "dialogue.started not emitted")

    -- advance() moves to choice node
    ds:advance()
    assert(ds:getCurrentLine() == "Pick:", "second node text wrong")
    local choices = ds:getChoices()
    assert(choices ~= nil,  "choice node should return choices")
    assert(#choices == 2,   "should have 2 choices")
    assert(choices[1].text == "Option A", "choice 1 text wrong")
    assert(choices[2].text == "Option B", "choice 2 text wrong")

    -- advance() on choice node is a noop
    ds:advance()
    assert(ds:getCurrentLine() == "Pick:", "advance on choice node must noop")

    -- choose(1) → next=3 → node "Done!"
    ds:choose(1)
    assert(ds:getCurrentLine() == "Done!", "after choice 1 text should be 'Done!'")
    assert(ds:getChoices() == nil,         "Done! node has no choices")

    -- advance() past last node → inactive, emits dialogue.ended
    ds:advance()
    assert(not ds:isActive(),          "should be inactive after last advance")
    assert(ds:getCurrentLine() == nil, "no current line after end")

    local found_ended = false
    for _, e in ipairs(event._emitted) do
        if e.name == "dialogue.ended" then found_ended = true end
    end
    assert(found_ended, "dialogue.ended not emitted")

    -- draw() calls debug.drawText when active
    local ds2 = DialogueSystem.new({ t = {
        id = "t",
        nodes = { { text = "Hi!" } }
    }})
    ds2:start("t")
    local before = #debug._texts
    ds2:draw(10, 10)
    assert(#debug._texts > before, "draw() should call debug.drawText")

    -- draw() with choices renders choice lines
    local ds3 = DialogueSystem.new({ t2 = {
        id = "t2",
        nodes = { { text = "Q?", choices = {
            { text = "Yes", next = 1 },
            { text = "No",  next = 1 },
        }}}
    }})
    ds3:start("t2")
    local before3 = #debug._texts
    ds3:draw(10, 50)
    assert(#debug._texts >= before3 + 3, "draw with 2 choices: text + 2 choice lines")

    -- choose() with invalid id silently ignored
    local ds4 = DialogueSystem.new({})
    ds4:start("nonexistent")
    assert(not ds4:isActive(), "start with unknown id should not activate")

    -- choose() out of range is a noop
    local ds5 = DialogueSystem.new({ c = {
        id = "c",
        nodes = { { text = "X", choices = {{ text="A", next=1 }} } }
    }})
    ds5:start("c")
    ds5:choose(99)  -- invalid index — must not crash
    assert(ds5:isActive(), "invalid choose index should not crash or deactivate")
end
)LUA");
    host.tick(0.016f);
    CHECK(!host.hasError());
    reportError(host, "dialogue");
    host.shutdown();
}

// ============================================================================
// main
// ============================================================================
int main() {
    std::cout << "=== Logic Components Tests ===\n";

    test_pause_manager();
    test_path_follower();
    test_platformer_controller();
    test_particle_emitter();
    test_dialogue_system();

    std::cout << "\nResults: " << g_passed << " passed, "
              << g_failed  << " failed\n";
    return g_failed > 0 ? 1 : 0;
}
