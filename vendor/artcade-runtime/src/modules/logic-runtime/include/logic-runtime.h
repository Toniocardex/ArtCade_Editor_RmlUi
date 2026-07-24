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
    explicit LogicRuntime(ILogicRuntimeHost& host, LogicRuntimeLimits limits = {});
    ~LogicRuntime();
    LogicRuntime(const LogicRuntime&) = delete;
    LogicRuntime& operator=(const LogicRuntime&) = delete;

    bool initialize(std::string* error = nullptr);
    bool loadPrograms(const std::vector<LogicProgram>& programs, std::string* error = nullptr);
    std::optional<ScopeToken> install(const ObjectTypeId& objectTypeId, EntityId owner,
                                      std::string* error = nullptr);
    bool cancelScope(ScopeToken token);
    /** Resets the aggregate per-frame event budget before input dispatch. */
    void beginFrame();
    void dispatchStart();
    /** Fires On Start only for subscriptions owned by @p owner (spawn path). */
    void dispatchStartForOwner(EntityId owner);
    void dispatchKeyPressed(LogicKey key);
    void dispatchKeyReleased(LogicKey key);
    void dispatchKeyHeld(LogicKey key);
    void dispatchCollisionEnter(EntityId owner, EntityId other);
    void dispatchCollisionExit(EntityId owner, EntityId other);
    /** Advances Every Second / Every Frame / Wait subscriptions. */
    void dispatchTick(float dt);
    void dispatchAnimationStarted(EntityId owner);
    void dispatchAnimationFinished(EntityId owner);
    void shutdown() noexcept;

    bool isEnabled() const;
    bool requiresTick() const;
    const std::vector<std::string>& diagnostics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ArtCade::Logic
