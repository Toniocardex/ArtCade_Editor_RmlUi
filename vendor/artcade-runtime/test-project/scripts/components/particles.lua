-- =============================================================================
-- ParticleEmitter — Logic Component
-- Pure-Lua particle system. Uses debug.drawCircle — no entity spawn needed.
--
-- Usage:
--   local pe = ParticleEmitter.new(x, y, {
--       count    = 20,
--       lifetime = 0.8,                    -- seconds each particle lives
--       speed    = { min=40, max=120 },    -- pixels/sec
--       spread   = math.pi * 2,            -- angle arc in radians (2*pi = full circle)
--       color    = "orange",
--       radius   = 3,                      -- particle draw radius in pixels
--       gravity  = 200,                    -- downward acceleration (px/s^2)
--       fadeOut  = true,                   -- shrink radius as particle dies
--   })
--   pe:burst()          -- spawn all particles in one frame
--   pe:update(dt)       -- advance simulation
--   pe:draw()           -- render via debug.drawCircle
--   pe:liveCount()      -- number of particles still alive
--   pe:isAlive()        -- false when all particles have expired
--   pe:moveTo(x, y)     -- reposition emitter origin
-- =============================================================================

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
    self.fadeOut  = (cfg.fadeOut ~= false)   -- default true
    self._particles = {}
    return self
end

-- Emit 'count' particles in one burst from the emitter origin.
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

-- Advance all particles by dt seconds; remove expired ones.
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

-- Draw all live particles using debug.drawCircle.
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

-- Number of particles still alive.
function ParticleEmitter:liveCount()
    return #self._particles
end

-- False when all particles have expired.
function ParticleEmitter:isAlive()
    return #self._particles > 0
end

-- Move the emitter origin (for burst at a new position).
function ParticleEmitter:moveTo(x, y)
    self.x = x
    self.y = y
end
