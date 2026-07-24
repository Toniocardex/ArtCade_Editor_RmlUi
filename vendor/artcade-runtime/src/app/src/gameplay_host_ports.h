#pragma once

// RU-02c host ports (docs/RU02_GAMEPLAY_SESSION_REFACTOR.md 4.3, editor repo):
// the narrow interfaces GameplaySession uses to reach host-owned services
// without depending on their concrete types (Modules::Audio,
// Modules::DialogManager, RuntimeProfiler). Application implements these
// via thin adapters (app_modules.h) wrapping the modules it still owns.
//
// Kept intentionally minimal: only the methods GameplaySession::tickFixedStep
// actually calls today. Widen only when a real caller needs the extra surface
// (e.g. IGameplayAudioService::playSound, once RuntimeLogicHostAdapter itself
// moves onto this port in a later slice).

#include <cstdint>

namespace ArtCade {

class IGameplayAudioService {
public:
    virtual ~IGameplayAudioService() = default;
    virtual void update() = 0;
};

class IGameplayDialogGate {
public:
    virtual ~IGameplayDialogGate() = default;
    virtual bool blocksGameplay() const = 0;
    virtual void tick(float dt) = 0;
};

class IRuntimeProfilerSink {
public:
    virtual ~IRuntimeProfilerSink() = default;
    virtual void addGameplayMs(double ms) = 0;
    virtual void addPhysicsMs(double ms) = 0;
    virtual void addLuaMs(double ms) = 0;
    virtual void addLuaEvents(std::uint32_t count) = 0;
    virtual void setLuaTickEnabled(bool enabled) = 0;
};

} // namespace ArtCade
