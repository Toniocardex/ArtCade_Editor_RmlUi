#pragma once

#include "../../../core/gameplay-runtime-host.h"
#include "script-core.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ArtCade::Scripts {

struct ScriptRuntimeLimits {
    std::size_t maxMemoryBytesPerScope = 4u * 1024u * 1024u;
    std::size_t maxSourceBytes = 1u * 1024u * 1024u;
    uint32_t maxInstructionsPerCallback = 100000;
    uint32_t maxCallDepth = 32;
    uint32_t maxScopes = 4096;
};

class ScriptRuntime {
public:
    explicit ScriptRuntime(ScriptRuntimeLimits limits = {});
    ScriptRuntime(IGameplayRuntimeHost& host, ScriptRuntimeLimits limits = {});
    ~ScriptRuntime();
    ScriptRuntime(ScriptRuntime&&) noexcept;
    ScriptRuntime& operator=(ScriptRuntime&&) noexcept;
    ScriptRuntime(const ScriptRuntime&) = delete;
    ScriptRuntime& operator=(const ScriptRuntime&) = delete;

    // Strict preflight for every linked saved program, including programs whose
    // Object Type has no instance in the selected scene.
    bool validateProgram(const ScriptProgram& program, std::string* error = nullptr) const;

    // One isolated VM per (entity, attachment). Installation order is dispatch
    // order and therefore must match the persisted attachment order.
    bool install(const ScriptProgram& program, EntityId owner,
                 ScriptAttachmentId attachmentId, std::string* error = nullptr);
    void dispatchStart();
    void dispatchInput(const ScriptInputSnapshot& input);
    void dispatchCollisionEnter(EntityId owner, EntityId other);
    void dispatchCollisionExit(EntityId owner, EntityId other);
    void update(float dt);
    void cancelOwner(EntityId owner);
    void shutdown() noexcept;

    std::size_t scopeCount() const;
    std::size_t activeScopeCount() const;
    const std::vector<ScriptRuntimeDiagnostic>& diagnostics() const;
    std::vector<ScriptRuntimeDiagnostic> drainDiagnostics();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ArtCade::Scripts
