#pragma once

#include "../../../core/gameplay-runtime-host.h"
#include "../../../core/types.h"
#include "../../logic-core/include/logic-core.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::Logic {

using ScopeToken = uint64_t;

struct LogicRuntimeLimits {
    std::size_t maxMemoryBytes = 16u * 1024u * 1024u;
    uint32_t maxInstructionsPerCallback = 100000;
    uint32_t maxEventDepth = 16;
    uint32_t maxScopes = 4096;
    uint32_t maxSubscriptions = 65536;
    uint32_t maxSubscriptionsPerScope = 128;
    uint32_t maxEventsPerDispatch = 4096;
};

// Compatibility name while call sites migrate to the shared host contract.
using ILogicRuntimeHost = IGameplayRuntimeHost;

class LogicRuntime {
public:
    // ADR-0037: sessionSeed is mandatory and owned by the caller (the
    // GameplaySession composition root in production, via
    // GameplaySessionSeed::make(); a fixed constant in tests/replays).
    // LogicRuntime never reads the clock or OS entropy itself.
    explicit LogicRuntime(ILogicRuntimeHost& host, uint32_t sessionSeed,
                          LogicRuntimeLimits limits = {});
    ~LogicRuntime();
    LogicRuntime(const LogicRuntime&) = delete;
    LogicRuntime& operator=(const LogicRuntime&) = delete;

    bool initialize(std::string* error = nullptr);
    bool loadPrograms(const std::vector<LogicProgram>& programs, std::string* error = nullptr);
    std::optional<ScopeToken> install(const ObjectTypeId& objectTypeId, EntityId owner,
                                      std::string* error = nullptr);
    std::optional<ScopeToken> installScene(const SceneId& sceneId, std::string* error = nullptr);
    bool cancelScope(ScopeToken token);
    /** Resets the aggregate per-frame event budget before input dispatch. */
    void beginFrame();
    void dispatchStart();
    /** Fires On Scene Start for the installed active-scene scope only. */
    void dispatchSceneStart(const SceneId& sceneId);
    /** Fires On Destroy for @p owner before the World tears down that entity. */
    void dispatchDestroy(EntityId owner);
    /** Fires On Start only for subscriptions owned by @p owner (spawn path). */
    void dispatchStartForOwner(EntityId owner);
    void dispatchKeyPressed(LogicKey key);
    void dispatchKeyReleased(LogicKey key);
    void dispatchKeyHeld(LogicKey key);
    void dispatchCollisionEnter(EntityId owner, EntityId other);
    void dispatchCollisionExit(EntityId owner, EntityId other);
    /**
     * Pre-simulation channel: Wait / Every Seconds timers + Pre Update rules.
     * Compatibility alias: dispatchTick → dispatchPreSimulationTick.
     */
    void dispatchPreSimulationTick(float dt);
    void dispatchTick(float dt);
    /**
     * Post-simulation level observers (Platformer State / Is Grounded / …).
     * Does not advance Wait / Every Seconds timers.
     */
    void dispatchPostSimulationTick(float dt);
    /** ADR-0055 pulse: On Landed (other = support; impact available in context). */
    void dispatchPlatformerLanded(EntityId owner, EntityId other, float landingImpactSpeed);
    /** ADR-0055 pulse: On Wall Blocked (side captured for contact_side / Event). */
    void dispatchPlatformerWallContact(EntityId owner, EntityId other,
                                       PlatformerWallSide side);
    /** ADR-0055 pulse: On Ceiling Hit. */
    void dispatchPlatformerCeilingHit(EntityId owner, EntityId other);
    void dispatchAnimationStarted(EntityId owner);
    void dispatchAnimationFinished(EntityId owner);
    void shutdown() noexcept;

    bool isEnabled() const;
    bool requiresTick() const;
    /** Undrained diagnostics accumulated since the last drain/shutdown. */
    const std::vector<std::string>& diagnostics() const;
    /**
     * ADR-0028 / RU-03: host-owned projection. Moves and clears the pending
     * diagnostic buffer. Expression rate-limit keys are retained so
     * expression_once stays once-per-key for the session.
     */
    std::vector<std::string> drainDiagnostics();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ArtCade::Logic
