#pragma once

#include <cstdint>

namespace ArtCade {

struct RuntimeProfileSnapshot {
    double luaMs = 0.0;
    double physicsMs = 0.0;
    double gameplayMs = 0.0;
    double renderMs = 0.0;
    uint32_t entityCount = 0;
    uint32_t activePhysicsBodies = 0;
    uint32_t luaEventCount = 0;
    bool luaTickEnabled = true;
};

class RuntimeProfiler {
public:
    void beginFrame() {
        current_ = {};
        current_.luaTickEnabled = last_.luaTickEnabled;
    }

    void addLuaMs(double value) { current_.luaMs += value; }
    void addPhysicsMs(double value) { current_.physicsMs += value; }
    void addGameplayMs(double value) { current_.gameplayMs += value; }
    void setRenderMs(double value) { current_.renderMs = value; }
    void addLuaEvents(uint32_t count) { current_.luaEventCount += count; }

    void setCounts(uint32_t entities, uint32_t physicsBodies) {
        current_.entityCount = entities;
        current_.activePhysicsBodies = physicsBodies;
    }

    void setLuaTickEnabled(bool enabled) {
        current_.luaTickEnabled = enabled;
    }

    void endFrame() { last_ = current_; }

    RuntimeProfileSnapshot snapshot() const { return last_; }

private:
    RuntimeProfileSnapshot current_{};
    RuntimeProfileSnapshot last_{};
};

} // namespace ArtCade
