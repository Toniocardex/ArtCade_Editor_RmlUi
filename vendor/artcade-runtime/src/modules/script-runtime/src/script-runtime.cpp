#include "../include/script-runtime.h"

#include "lua-host.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace ArtCade::Scripts {
namespace {

using ArtCade::Modules::LuaHost;
using ArtCade::Modules::LuaHostOptions;
using ArtCade::Modules::LuaSandboxProfile;

std::vector<LogicKey> normalizedKeys(const std::vector<LogicKey>& keys) {
    std::vector<LogicKey> normalized;
    normalized.reserve(keys.size());
    for (LogicKey key : keys) {
        const int value = static_cast<int>(key);
        if (value < static_cast<int>(LogicKey::A)
            || value > static_cast<int>(LogicKey::Enter)) continue;
        normalized.push_back(key);
    }
    std::sort(normalized.begin(), normalized.end(), [](LogicKey a, LogicKey b) {
        return static_cast<int>(a) < static_cast<int>(b);
    });
    normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
    return normalized;
}

ScriptInputSnapshot normalizedInput(const ScriptInputSnapshot& input) {
    return ScriptInputSnapshot{
        normalizedKeys(input.pressed),
        normalizedKeys(input.released),
        normalizedKeys(input.held)};
}

std::unique_ptr<LuaHost> loadIsolatedHost(
    const ScriptProgram& program,
    const ScriptRuntimeLimits& limits,
    std::string* error) {
    if (program.assetId.empty() || program.sourcePath.empty()) {
        if (error) *error = "Script program identity is incomplete";
        return nullptr;
    }
    if (limits.maxMemoryBytesPerScope == 0 || limits.maxSourceBytes == 0
        || limits.maxInstructionsPerCallback == 0 || limits.maxCallDepth == 0
        || limits.maxScopes == 0) {
        if (error) *error = "Script runtime limits must be finite and non-zero";
        return nullptr;
    }
    if (program.source.size() > limits.maxSourceBytes) {
        if (error) *error = "Script source exceeds the configured size limit";
        return nullptr;
    }
    auto host = std::make_unique<LuaHost>(LuaHostOptions{
        LuaSandboxProfile::ManualScriptStrict, limits.maxMemoryBytesPerScope});
    if (!host->init()) {
        if (error) *error = "Could not initialize the manual Script VM";
        return nullptr;
    }
    if (!host->loadManualProgramSource(
            program.source, program.sourcePath, kScriptApiVersion,
            limits.maxInstructionsPerCallback, limits.maxCallDepth)) {
        if (error) {
            *error = host->memoryLimitExceeded()
                ? "Script memory limit exceeded while loading " + program.sourcePath
                : host->lastError();
        }
        return nullptr;
    }
    return host;
}

} // namespace

struct ScriptRuntime::Impl {
    struct Scope {
        EntityId owner = INVALID_ENTITY;
        ScriptAttachmentId attachmentId;
        AssetId scriptAssetId;
        std::string sourcePath;
        std::unique_ptr<LuaHost> host;
        bool active = true;
    };

    explicit Impl(ScriptRuntimeLimits value, IGameplayRuntimeHost* runtimeHost = nullptr)
        : limits(value), host(runtimeHost) {}

    void disable(Scope& scope, ScriptRuntimePhase phase,
                 std::string callback, std::string message) {
        scope.active = false;
        ScriptRuntimeDiagnostic diagnostic;
        diagnostic.owner = scope.owner;
        diagnostic.attachmentId = scope.attachmentId;
        diagnostic.scriptAssetId = scope.scriptAssetId;
        diagnostic.sourcePath = scope.sourcePath;
        diagnostic.phase = phase;
        diagnostic.line = scriptDiagnosticLine(message);
        diagnostic.column = diagnostic.line > 0 ? 1 : 0;
        diagnostic.callback = std::move(callback);
        diagnostic.message = std::move(message);
        diagnostics.push_back(std::move(diagnostic));
    }

    template<typename Invoke>
    void dispatchCollisionToOwner(EntityId owner, EntityId other,
                                  const char* callback, Invoke invoke) {
        if (!startDispatched || owner == INVALID_ENTITY
            || other == INVALID_ENTITY || owner == other) return;
        for (Scope& scope : scopes) {
            if (!scope.active || scope.owner != owner) continue;
            if (invoke(*scope.host, scope.owner, other)) continue;
            const std::string message = scope.host->memoryLimitExceeded()
                ? "Script memory limit exceeded in " + std::string(callback)
                : scope.host->lastError();
            disable(scope, ScriptRuntimePhase::Collision, callback, message);
        }
    }

    ScriptRuntimeLimits limits;
    IGameplayRuntimeHost* host = nullptr;
    std::vector<Scope> scopes;
    std::vector<ScriptRuntimeDiagnostic> diagnostics;
    ScriptInputSnapshot input;
    bool startDispatched = false;
};

ScriptRuntime::ScriptRuntime(ScriptRuntimeLimits limits)
    : impl_(std::make_unique<Impl>(limits)) {}
ScriptRuntime::ScriptRuntime(IGameplayRuntimeHost& host, ScriptRuntimeLimits limits)
    : impl_(std::make_unique<Impl>(limits, &host)) {}
ScriptRuntime::~ScriptRuntime() = default;
ScriptRuntime::ScriptRuntime(ScriptRuntime&&) noexcept = default;
ScriptRuntime& ScriptRuntime::operator=(ScriptRuntime&&) noexcept = default;

bool ScriptRuntime::validateProgram(const ScriptProgram& program, std::string* error) const {
    return static_cast<bool>(loadIsolatedHost(program, impl_->limits, error));
}

bool ScriptRuntime::install(const ScriptProgram& program, EntityId owner,
                            ScriptAttachmentId attachmentId, std::string* error) {
    if (impl_->startDispatched) {
        if (error) *error = "Cannot install Script scopes after on_start dispatch";
        return false;
    }
    if (owner == INVALID_ENTITY || attachmentId.empty()) {
        if (error) *error = "Script scope identity is invalid";
        return false;
    }
    if (impl_->scopes.size() >= impl_->limits.maxScopes) {
        if (error) *error = "Script scope limit exceeded";
        return false;
    }
    std::unique_ptr<LuaHost> host = loadIsolatedHost(program, impl_->limits, error);
    if (!host) return false;
    impl_->scopes.push_back(Impl::Scope{
        owner, std::move(attachmentId), program.assetId, program.sourcePath,
        std::move(host), true});
    return true;
}

void ScriptRuntime::dispatchStart() {
    if (impl_->startDispatched) return;
    impl_->startDispatched = true;
    for (Impl::Scope& scope : impl_->scopes) {
        if (!scope.active) continue;
        if (!scope.host->callManualOnStart(
                impl_->host, scope.owner, impl_->limits.maxInstructionsPerCallback,
                impl_->limits.maxCallDepth)) {
            const std::string message = scope.host->memoryLimitExceeded()
                ? "Script memory limit exceeded in on_start"
                : scope.host->lastError();
            impl_->disable(scope, ScriptRuntimePhase::Start, "on_start", message);
        }
    }
}

void ScriptRuntime::dispatchInput(const ScriptInputSnapshot& input) {
    impl_->input = normalizedInput(input);
    if (!impl_->startDispatched) return;

    const auto dispatch = [&](const std::vector<LogicKey>& keys,
                              const char* callback,
                              auto invoke) {
        for (LogicKey key : keys) {
            for (Impl::Scope& scope : impl_->scopes) {
                if (!scope.active) continue;
                if (invoke(*scope.host, scope.owner, key)) continue;
                const std::string message = scope.host->memoryLimitExceeded()
                    ? "Script memory limit exceeded in " + std::string(callback)
                    : scope.host->lastError();
                impl_->disable(scope, ScriptRuntimePhase::Input, callback, message);
            }
        }
    };

    dispatch(impl_->input.pressed, "on_key_pressed",
        [&](LuaHost& host, EntityId owner, LogicKey key) {
            return host.callManualOnKeyPressed(
                impl_->host, owner, key, impl_->input,
                impl_->limits.maxInstructionsPerCallback, impl_->limits.maxCallDepth);
        });
    dispatch(impl_->input.released, "on_key_released",
        [&](LuaHost& host, EntityId owner, LogicKey key) {
            return host.callManualOnKeyReleased(
                impl_->host, owner, key, impl_->input,
                impl_->limits.maxInstructionsPerCallback, impl_->limits.maxCallDepth);
        });
    dispatch(impl_->input.held, "on_key_held",
        [&](LuaHost& host, EntityId owner, LogicKey key) {
            return host.callManualOnKeyHeld(
                impl_->host, owner, key, impl_->input,
                impl_->limits.maxInstructionsPerCallback, impl_->limits.maxCallDepth);
        });
}

void ScriptRuntime::dispatchCollisionEnter(EntityId owner, EntityId other) {
    impl_->dispatchCollisionToOwner(owner, other, "on_collision_enter",
        [&](LuaHost& host, EntityId self, EntityId eventOther) {
            return host.callManualOnCollisionEnter(
                impl_->host, self, eventOther, impl_->input,
                impl_->limits.maxInstructionsPerCallback, impl_->limits.maxCallDepth);
        });
}

void ScriptRuntime::dispatchCollisionExit(EntityId owner, EntityId other) {
    impl_->dispatchCollisionToOwner(owner, other, "on_collision_exit",
        [&](LuaHost& host, EntityId self, EntityId eventOther) {
            return host.callManualOnCollisionExit(
                impl_->host, self, eventOther, impl_->input,
                impl_->limits.maxInstructionsPerCallback, impl_->limits.maxCallDepth);
        });
}

void ScriptRuntime::update(float dt) {
    if (!impl_->startDispatched || !std::isfinite(dt) || dt <= 0.f) return;
    for (Impl::Scope& scope : impl_->scopes) {
        if (!scope.active || !scope.host->hasManualOnUpdate()) continue;
        if (!scope.host->callManualOnUpdate(
                impl_->host, scope.owner, dt, impl_->input,
                impl_->limits.maxInstructionsPerCallback,
                impl_->limits.maxCallDepth)) {
            const std::string message = scope.host->memoryLimitExceeded()
                ? "Script memory limit exceeded in on_update"
                : scope.host->lastError();
            impl_->disable(scope, ScriptRuntimePhase::Update, "on_update", message);
        }
    }
}

void ScriptRuntime::cancelOwner(EntityId owner) {
    for (Impl::Scope& scope : impl_->scopes) {
        if (scope.owner == owner) scope.active = false;
    }
}

void ScriptRuntime::shutdown() noexcept {
    impl_->scopes.clear();
    impl_->diagnostics.clear();
    impl_->input = {};
    impl_->startDispatched = false;
}

std::size_t ScriptRuntime::scopeCount() const { return impl_->scopes.size(); }

std::size_t ScriptRuntime::activeScopeCount() const {
    return static_cast<std::size_t>(std::count_if(
        impl_->scopes.begin(), impl_->scopes.end(),
        [](const Impl::Scope& scope) { return scope.active; }));
}

const std::vector<ScriptRuntimeDiagnostic>& ScriptRuntime::diagnostics() const {
    return impl_->diagnostics;
}

std::vector<ScriptRuntimeDiagnostic> ScriptRuntime::drainDiagnostics() {
    std::vector<ScriptRuntimeDiagnostic> result;
    result.swap(impl_->diagnostics);
    return result;
}

} // namespace ArtCade::Scripts
