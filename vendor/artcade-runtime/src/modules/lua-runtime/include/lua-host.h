#pragma once

#include "../../../core/module.h"
#include "../../../core/types.h"
#include "../../../core/gameplay-runtime-host.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstddef>

// Forward-declare sol::state so BindingCallback can reference it without
// pulling all of Sol2 into every translation unit that includes this header.
// The full definition is only needed in lua-host.cpp and game-api.cpp.
namespace sol { class state; }
namespace ArtCade::Scripts { struct ScriptInputSnapshot; }

namespace ArtCade::Modules {

enum class LuaSandboxProfile {
    LegacyGameplay,
    LogicBoardStrict,
    ManualScriptStrict,
};

struct LuaHostOptions {
    LuaSandboxProfile profile = LuaSandboxProfile::LegacyGameplay;
    // Zero keeps the legacy unbounded allocator. Logic Board always supplies a
    // finite cap and receives a controlled allocation failure when it is hit.
    std::size_t maxMemoryBytes = 0;
};

/**
 * LuaHost — Lua 5.4 VM wrapper.
 *
 * Loads compiled bytecode (.luac) and drives the game-logic tick.
 * Sol2 and Lua internals are confined to src/lua-host.cpp via Pimpl.
 */
class LuaHost final : public IModule {
public:
    explicit LuaHost(LuaHostOptions options = {});
    ~LuaHost();  // defined in .cpp where Impl (and sol::state) are complete

    bool init()     override;
    void shutdown() override;

    // Register bindings before loading any script.
    // Callback receives a sol::state& to register C++ ↔ Lua functions.
    using BindingCallback = std::function<void(sol::state&)>;
    void registerBindings(BindingCallback cb);

    // Load compiled Lua bytecode from an in-memory buffer
    // (the disk-path variant was removed — all production callers go
    // through AssetLoader::loadLuaBytecode + this buffer overload).
    bool loadBytecodeBuffer(const uint8_t* data, size_t size);

    // Hot-reload: load and execute Lua SOURCE text (not bytecode).
    // Re-defines globals such as tick() — used by the Logic Board editor's
    // "Apply & Hot-Reload". Returns false and sets lastError() on syntax/
    // runtime error, leaving the previously loaded script intact.
    bool loadLuaSource(const std::string& sourceCode);

    // Strict manual-script contract. The source chunk must call
    // artcade.require_api_version() and return a table whose optional
    // supported lifecycle fields are functions. Calls are protected and bounded.
    bool loadManualProgramSource(const std::string& sourceCode,
                                 const std::string& sourcePath,
                                 uint32_t supportedApiVersion,
                                 uint32_t maxInstructions,
                                 uint32_t maxCallDepth);
    bool callManualOnStart(IGameplayRuntimeHost* host, EntityId owner,
                           uint32_t maxInstructions,
                           uint32_t maxCallDepth);
    bool callManualOnKeyPressed(IGameplayRuntimeHost* host, EntityId owner,
                                LogicKey key,
                                const Scripts::ScriptInputSnapshot& input,
                                uint32_t maxInstructions, uint32_t maxCallDepth);
    bool callManualOnKeyReleased(IGameplayRuntimeHost* host, EntityId owner,
                                 LogicKey key,
                                 const Scripts::ScriptInputSnapshot& input,
                                 uint32_t maxInstructions, uint32_t maxCallDepth);
    bool callManualOnKeyHeld(IGameplayRuntimeHost* host, EntityId owner,
                             LogicKey key,
                             const Scripts::ScriptInputSnapshot& input,
                             uint32_t maxInstructions, uint32_t maxCallDepth);
    bool callManualOnCollisionEnter(IGameplayRuntimeHost* host, EntityId owner,
                                    EntityId other,
                                    const Scripts::ScriptInputSnapshot& input,
                                    uint32_t maxInstructions, uint32_t maxCallDepth);
    bool callManualOnCollisionExit(IGameplayRuntimeHost* host, EntityId owner,
                                   EntityId other,
                                   const Scripts::ScriptInputSnapshot& input,
                                   uint32_t maxInstructions, uint32_t maxCallDepth);
    bool callManualOnUpdate(IGameplayRuntimeHost* host, EntityId owner, float dt,
                            const Scripts::ScriptInputSnapshot& input,
                            uint32_t maxInstructions,
                            uint32_t maxCallDepth);
    bool hasManualOnUpdate() const;

    // Execute the global "tick" function (called every fixed step)
    void tick(float dt);
    bool isScriptTickRequired() const;

    bool        hasError()  const { return !lastError_.empty(); }
    std::string lastError() const { return lastError_; }
    void        clearError()      { lastError_.clear(); }
    bool        memoryLimitExceeded() const;

private:
    struct Impl;               // defined in lua-host.cpp
    std::unique_ptr<Impl> impl_;
    LuaHostOptions options_;

    std::string                  lastError_;
    std::vector<BindingCallback> pendingBindings_;
};

} // namespace ArtCade::Modules
