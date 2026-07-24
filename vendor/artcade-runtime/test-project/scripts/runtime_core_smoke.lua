-- Runtime Core smoke: scene.load preserves state.* (run via game.exe / WASM with multi-scene project)
local function log(msg)
    if debug and debug.log then debug.log("[smoke] " .. msg) end
end

function init()
    global.set("score", 100)
    log("init score=100")
end

function tick(dt)
    if global.get("_smoke_done") then return end
    local before = global.get("score")
    if before ~= 100 then
        log("FAIL score before load: " .. tostring(before))
        global.set("_smoke_done", true)
        return
    end
    if scene and scene.load then
        scene.load("scene_b")
        local after = global.get("score")
        if after == 100 then
            log("PASS scene.load preserved score")
        else
            log("FAIL score after load: " .. tostring(after))
        end
    else
        log("SKIP scene.load not bound")
    end
    global.set("_smoke_done", true)
end
