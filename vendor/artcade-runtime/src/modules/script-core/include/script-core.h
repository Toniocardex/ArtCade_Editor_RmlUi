#pragma once

#include "../../../core/types.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace ArtCade::Scripts {

inline constexpr uint32_t kScriptApiVersion = 1;

// Immutable source snapshot supplied at Start Play. Runtime code never reads
// ProjectDocument, editor buffers or the filesystem after materialization.
struct ScriptProgram {
    AssetId assetId;
    std::string sourcePath;
    std::string source;
};

// Immutable per-frame input projection. Producers may submit keys in any order;
// ScriptRuntime normalizes and deduplicates them before dispatch.
struct ScriptInputSnapshot {
    std::vector<LogicKey> pressed;
    std::vector<LogicKey> released;
    std::vector<LogicKey> held;
};

enum class ScriptRuntimePhase { Load, Start, Input, Collision, Update };

inline int scriptDiagnosticLine(const std::string& message) {
    for (std::size_t colon = message.find(':'); colon != std::string::npos;
         colon = message.find(':', colon + 1)) {
        std::size_t cursor = colon + 1;
        unsigned long long value = 0;
        bool found = false;
        while (cursor < message.size() && message[cursor] >= '0'
               && message[cursor] <= '9') {
            found = true;
            const unsigned digit = static_cast<unsigned>(message[cursor] - '0');
            const auto max = static_cast<unsigned long long>(std::numeric_limits<int>::max());
            if (value > (max - digit) / 10u)
                return 0;
            value = value * 10u + digit;
            ++cursor;
        }
        if (found && cursor < message.size() && message[cursor] == ':' && value > 0)
            return static_cast<int>(value);
    }
    return 0;
}

struct ScriptRuntimeDiagnostic {
    EntityId owner = INVALID_ENTITY;
    ScriptAttachmentId attachmentId;
    AssetId scriptAssetId;
    std::string sourcePath;
    ScriptRuntimePhase phase = ScriptRuntimePhase::Load;
    int line = 0;
    int column = 0;
    std::string callback;
    std::string message;
};

} // namespace ArtCade::Scripts
