-- =============================================================================
-- ArtCade Phase 13 - Integration + Physics Demo
-- Controls: WASD / Arrow keys to move player
-- Physics:  green ball falls under gravity, rests on orange floor
-- Logging:  writes event logs to the editor console; optional heartbeat rows
--           stay file-only by default to avoid noisy UI updates.
-- =============================================================================

print("[Demo] ArtCade Phase 13 + Physics Demo loaded!")

-- ---------------------------------------------------------------------------
-- Constants
-- ---------------------------------------------------------------------------
local SPEED        = 220
local PLAYER_SIZE  = 26
local ENEMY_SIZE   = 20
local COIN_SIZE    = 12
local COIN_RANGE   = 30
local ENEMY_RANGE  = 50
local BALL_SIZE    = 22          -- half-extent (box is 44x44)
local FLOOR_H      = 20          -- half-height of static floor
local FLOOR_W      = 640         -- half-width  of static floor (full: 1280)
local SCREEN_W     = 1280
local SCREEN_H     = 720
local GRAVITY_Y    = 500         -- px/s^2  (Y-down)
local LOG_INTERVAL = 2           -- seconds between heartbeat log lines
local FILE_LOGGING_ENABLED = true
local HEARTBEAT_TO_CONSOLE = false

-- ---------------------------------------------------------------------------
-- Runtime state
-- ---------------------------------------------------------------------------
local t          = 0
local frameCount = 0
local score      = 0
local alive      = true

local playerId   = nil
local enemyIds   = {}
local coinIds    = {}
local ballId     = nil
local floorId    = nil
local enemyData  = {}
local moveKeys   = {}
local wasTouchingEnemy = false
local writeHeartbeat

-- Log file handle (opened in init, closed on last frame)
local logFile    = nil
local logPath    = "test-project/logs/physics_test.log"

-- ---------------------------------------------------------------------------
-- Helpers
-- ---------------------------------------------------------------------------
local function clamp(v, lo, hi)
    return math.max(lo, math.min(hi, v))
end

local function removeCoinId(cid)
    for i, id in ipairs(coinIds) do
        if id == cid then
            table.remove(coinIds, i)
            break
        end
    end
end

local function updateCollisionInteractions()
    if not playerId then return end

    local coinId = collision.firstTouching(playerId, {
        response = "sensor",
        className = "Coin"
    })
    if coinId and coinId ~= 0 then
        score = score + 10
        global.set("score", score)
        log("Coin collected! score=" .. tostring(score))
        entity.destroy(coinId)
        removeCoinId(coinId)
    end

    local enemyId = collision.firstTouching(playerId, {
        response = "sensor",
        className = "Enemy"
    })
    local touchingEnemy = enemyId and enemyId ~= 0
    if touchingEnemy and not wasTouchingEnemy and alive then
        local damage = 34
        local nextHp = math.max(0, (global.get("playerHp") or 0) - damage)
        global.set("playerHp", nextHp)
        if nextHp <= 0 then
            alive = false
            global.set("alive", 0)
            log("PLAYER KO by enemy " .. tostring(enemyId)
                .. "  score=" .. tostring(score))
        else
            log(string.format("PLAYER HIT hp=%.0f/100", nextHp))
        end
    elseif not touchingEnemy and wasTouchingEnemy then
        if (global.get("playerHp") or 0) > 0 then
            alive = true
            global.set("alive", 1)
        end
    end
    wasTouchingEnemy = touchingEnemy
end

local function bindMovementInput()
    local codes = {
        "KeyW", "ArrowUp",
        "KeyS", "ArrowDown",
        "KeyA", "ArrowLeft",
        "KeyD", "ArrowRight"
    }

    for _, code in ipairs(codes) do
        moveKeys[code] = input.isKeyDown(code)
        input.onPressed(code, function() moveKeys[code] = true end)
        input.onReleased(code, function() moveKeys[code] = false end)
    end
end

local function movementAxis()
    local dx, dy = 0, 0
    if moveKeys["KeyW"] or moveKeys["ArrowUp"]    then dy = dy - 1 end
    if moveKeys["KeyS"] or moveKeys["ArrowDown"]  then dy = dy + 1 end
    if moveKeys["KeyA"] or moveKeys["ArrowLeft"]  then dx = dx - 1 end
    if moveKeys["KeyD"] or moveKeys["ArrowRight"] then dx = dx + 1 end
    if dx ~= 0 and dy ~= 0 then dx = dx*0.7071; dy = dy*0.7071 end
    return dx, dy
end

-- Write a line to the editor console and log file.
-- Pass false as the second argument for file-only diagnostic rows.
local function log(msg, toConsole)
    if toConsole ~= false then
        debug.log(msg)
    end
    if logFile then
        logFile:write("[Lua] " .. msg .. "\n")
        logFile:flush()
    end
end

-- ---------------------------------------------------------------------------
-- Init
-- ---------------------------------------------------------------------------
local function init()
    -- File logging is optional: packaged/WASM runtimes may not expose this path.
    if FILE_LOGGING_ENABLED then
        logFile = io.open(logPath, "w")
    end
    if logFile then
        logFile:write("=== ArtCade Phase 13 Physics Test Log ===\n")
        logFile:write("GRAVITY_Y = " .. tostring(GRAVITY_Y) .. " px/s^2\n")
        logFile:write("BALL start pos: (640, 60)\n")
        logFile:write("FLOOR pos:      (640, 640)  size: 1280x40  [static]\n")
        logFile:write("------------------------------------------\n")
        logFile:flush()
    end

    -- ---- Position-based entities ----
    local players = pool.getAll("Player")
    if #players > 0 then
        playerId = players[1]
        log("Player found  id=" .. tostring(playerId))
    else
        log("WARNING: no Player entity!")
    end

    enemyIds = pool.getAll("Enemy")
    log("Enemies: " .. tostring(#enemyIds))
    for _, eid in ipairs(enemyIds) do
        local ex, ey = entity.position(eid)
        enemyData[eid] = { ox=ex, oy=ey,
                           dir=(eid % 2 == 0 and 1 or -1),
                           speed=60 + eid*20, range=180 }
    end

    coinIds = pool.getAll("Coin")
    log("Coins:   " .. tostring(#coinIds))

    global.set("score", 0)
    global.set("level", 1)
    global.set("playerName", "Hero")
    global.set("hardMode", false)
    global.set("playerHp", 100)
    bindMovementInput()

    if playerId then
        log(string.format("Player hp: %.0f/100", global.get("playerHp") or 0))
    end

    -- Verify declared global values.
    local sc = global.get("score")
    local lv = global.get("level")
    local nm = global.get("playerName")
    local hm = global.get("hardMode")
    local nx = global.get("nonexistent")
    if sc == 0 and lv == 1 and nm == "Hero" and hm == false and nx == nil then
        log("global.get() OK: score=" .. tostring(sc) .. " level=" .. tostring(lv)
            .. " name=" .. tostring(nm) .. " hard=" .. tostring(hm)
            .. " nonexistent=" .. tostring(nx))
    else
        log("global.get() FAIL: sc=" .. tostring(sc) .. " lv=" .. tostring(lv)
            .. " nm=" .. tostring(nm) .. " hm=" .. tostring(hm)
            .. " nx=" .. tostring(nx))
    end

    -- Save/Load round-trip test (via SaveLoadManager)
    local saveSlot = "test_slot"
    global.set("score", 42)
    local writeOK = save.writeGame(saveSlot)
    global.set("score", 0)
    local loadOK = save.loadGame(saveSlot)
    log("save.writeGame/loadGame -> " .. tostring(writeOK and loadOK)
        .. ", score=" .. tostring(global.get("score")))

    log("save.exists -> " .. tostring(save.exists(saveSlot)))

    local slots = save.list()
    log("save.list  -> " .. tostring(#slots) .. " slot(s)")

    save.delete(saveSlot)
    log("save.delete -> exists now? " .. tostring(save.exists(saveSlot)))

    -- ---- Physics world ----
    physics.setGravity(0, GRAVITY_Y)
    log("Gravity  (0, " .. tostring(GRAVITY_Y) .. ") px/s^2")

    local floors = pool.getAll("PhysicsFloor")
    if #floors > 0 then
        floorId = floors[1]
        local fx, fy = entity.position(floorId)
        log(string.format("Floor body from ProjectDoc  pos=(%.0f,%.0f)  size=%dx%d",
            fx, fy, FLOOR_W*2, FLOOR_H*2))
    else
        log("WARNING: no PhysicsFloor entity!")
    end

    local balls = pool.getAll("PhysicsBall")
    if #balls > 0 then
        ballId = balls[1]
        local bx, by = entity.position(ballId)
        log(string.format("Ball body from ProjectDoc  pos=(%.0f,%.0f)  size=%dx%d",
            bx, by, BALL_SIZE*2, BALL_SIZE*2))
        log("Expected rest y = " .. tostring((640 - FLOOR_H) - BALL_SIZE))
    else
        log("WARNING: no PhysicsBall entity!")
    end

    log("Init OK - WASD to move player, watch ball fall")
    time.every(LOG_INTERVAL, writeHeartbeat)
end

-- ---------------------------------------------------------------------------
-- Update enemies (horizontal patrol)
-- ---------------------------------------------------------------------------
local function updateEnemies(dt)
    for _, eid in ipairs(enemyIds) do
        local d = enemyData[eid]
        if d then
            local ex, ey = entity.position(eid)
            ex = ex + d.dir * d.speed * dt
            if     ex > d.ox + d.range then ex = d.ox + d.range; d.dir = -1
            elseif ex < d.ox - d.range then ex = d.ox - d.range; d.dir =  1
            end
            entity.setPosition(eid, ex, ey)
        end
    end
end

-- ---------------------------------------------------------------------------
-- Draw
-- ---------------------------------------------------------------------------
local function drawScene()
    -- Screen border
    debug.drawLine(1,          1,          SCREEN_W-1, 1,          "white")
    debug.drawLine(SCREEN_W-1, 1,          SCREEN_W-1, SCREEN_H-1, "white")
    debug.drawLine(SCREEN_W-1, SCREEN_H-1, 1,          SCREEN_H-1, "white")
    debug.drawLine(1,          SCREEN_H-1, 1,          1,          "white")

    -- Player
    if playerId then
        local px, py = entity.position(playerId)
        local col = alive and "blue" or "magenta"
        debug.drawRect(px-PLAYER_SIZE, py-PLAYER_SIZE,
                       PLAYER_SIZE*2,  PLAYER_SIZE*2, col)
        debug.drawRect(px-8,  py-7, 5, 5, "white")   -- left eye
        debug.drawRect(px+4,  py-7, 5, 5, "white")   -- right eye
    end

    -- Enemies
    for _, eid in ipairs(enemyIds) do
        local ex, ey = entity.position(eid)
        debug.drawRect(ex-ENEMY_SIZE, ey-ENEMY_SIZE,
                       ENEMY_SIZE*2,  ENEMY_SIZE*2, "red")
        local r = ENEMY_RANGE   -- danger radius outline
        debug.drawLine(ex-r, ey-r, ex+r, ey-r, "red")
        debug.drawLine(ex+r, ey-r, ex+r, ey+r, "red")
        debug.drawLine(ex+r, ey+r, ex-r, ey+r, "red")
        debug.drawLine(ex-r, ey+r, ex-r, ey-r, "red")
    end

    -- Coins
    for _, cid in ipairs(coinIds) do
        local cx, cy = entity.position(cid)
        debug.drawRect(cx-COIN_SIZE, cy-COIN_SIZE,
                       COIN_SIZE*2,  COIN_SIZE*2, "yellow")
    end

    -- Physics floor (orange bar)
    if floorId then
        local fx, fy = entity.position(floorId)
        debug.drawRect(fx-FLOOR_W, fy-FLOOR_H, FLOOR_W*2, FLOOR_H*2, "orange")
    end

    -- Physics ball (green box + crosshair)
    if ballId then
        local bx, by = entity.position(ballId)
        debug.drawRect(bx-BALL_SIZE, by-BALL_SIZE,
                       BALL_SIZE*2,  BALL_SIZE*2, "green")
        debug.drawLine(bx-BALL_SIZE-6, by, bx+BALL_SIZE+6, by, "cyan")
        debug.drawLine(bx, by-BALL_SIZE-6, bx, by+BALL_SIZE+6, "cyan")
    end
end

-- ---------------------------------------------------------------------------
-- Heartbeat log row (stdout + file)
-- ---------------------------------------------------------------------------
function writeHeartbeat()
    local px, py = 0, 0
    if playerId then px, py = entity.position(playerId) end

    local bx, by, vx, vy = 0, 0, 0, 0
    if ballId then
        bx, by = entity.position(ballId)
        vx, vy = entity.velocity(ballId)
    end

    local fps = (t > 0) and math.floor(frameCount / t + 0.5) or 0

    -- Determine ball state
    local ballState = "FALLING"
    local restY = (640 - FLOOR_H) - BALL_SIZE   -- expected = 598
    if math.abs(by - restY) < 2 and math.abs(vy) < 2 then
        ballState = "AT_REST"
    elseif vy < 0 then
        ballState = "BOUNCING_UP"
    end

    local row = string.format(
        "t=%3.0fs  ball=(%.1f,%.1f) vy=%5.1f [%s]  player=(%.0f,%.0f)  score=%d  coins=%d  fps=%d",
        t, bx, by, vy, ballState, px, py, score, #coinIds, fps
    )
    log(row, HEARTBEAT_TO_CONSOLE)
end

-- ---------------------------------------------------------------------------
-- Main tick
-- ---------------------------------------------------------------------------
local initialized = false

function tick(dt)
    if not initialized then
        init()
        initialized = true
    end

    t          = t + dt
    frameCount = frameCount + 1

    updateEnemies(dt)

    -- Player movement
    if playerId and alive then
        local px, py = entity.position(playerId)
        local dx, dy = movementAxis()
        px = clamp(px + dx*SPEED*dt, PLAYER_SIZE, SCREEN_W-PLAYER_SIZE)
        py = clamp(py + dy*SPEED*dt, PLAYER_SIZE, SCREEN_H-PLAYER_SIZE)
        entity.setPosition(playerId, px, py)
    end

    updateCollisionInteractions()

    drawScene()
end
