#pragma once

#include "script-core.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ArtCade::Scripts {

// Tooling projection of the Manual Script API surface. Not ProjectDocument,
// not Play authority, and not a second executor — lua-host bindings remain
// the runtime source of truth; parity tests keep the two aligned.

enum class ScriptApiKind {
    GlobalFn,
    Callback,
    CtxField,
    Method,
};

// How the Script API panel / catalog insert should mutate source.
enum class ScriptApiInsertKind {
    None,              // documentation only
    Expression,        // ctx.self, ctx.entity_id
    FunctionCall,      // ctx.self.set_rotation(
    LifecycleCallback, // on_start / on_update … into return { }
    ApiDeclaration,    // artcade.require_api_version once at file head
};

enum class ScriptApiUnit {
    None,
    World,
    Radians,
    Seconds,
    NormalizedAxis,
    Volume01,
    Multiplier,
};

struct ScriptApiParam {
    const char* name = "";
    const char* luaType = "number"; // "number"|"boolean"|"string"|"table"|…
    ScriptApiUnit unit = ScriptApiUnit::None;
    bool optional = false;
    const char* defaultLiteral = nullptr; // e.g. "1.0"
};

struct ScriptApiEntry {
    uint32_t apiVersion = kScriptApiVersion;
    ScriptApiKind kind = ScriptApiKind::Method;
    ScriptApiInsertKind insertKind = ScriptApiInsertKind::None;
    const char* qualifiedName = ""; // "ctx.self.set_rotation"
    const char* parentPath = "";    // "ctx.self"
    const char* name = "";          // "set_rotation"
    const char* signature = "";     // "set_rotation(radians)"
    const char* insertText = "";    // completion / snippet insert
    const char* shortDoc = "";
    const ScriptApiParam* params = nullptr;
    std::size_t paramCount = 0;
    const char* returnType = "nil";
};

const std::vector<ScriptApiEntry>& scriptApiCatalogV1();

const ScriptApiEntry* findScriptApiByQualifiedName(const std::string& qualifiedName);

// Qualified paths that must match lua-host Manual Script registration.
std::vector<std::string> scriptApiCatalogBindingPaths();

// Runtime inventory built from the same tables as pushManualContext / capture.
// Declared here so editor/tests depend on script-core; defined in lua-host.cpp.
std::vector<std::string> manualScriptRuntimeBindingInventory();

std::string scriptApiUnitLabel(ScriptApiUnit unit);
std::string formatScriptApiSignatureHelp(const ScriptApiEntry& entry);
std::string formatScriptApiHover(const ScriptApiEntry& entry);

} // namespace ArtCade::Scripts
