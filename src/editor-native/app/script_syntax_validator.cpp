#include "editor-native/app/script_syntax_validator.h"

#include "editor-native/app/project_script_file_service.h"
#include "editor-native/model/project_document.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cctype>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <string>
#include <unordered_set>

namespace ArtCade::EditorNative {
namespace {

struct LuaStateCloser {
    void operator()(lua_State* state) const { if (state) lua_close(state); }
};

int extractLine(const std::string& error) {
    for (std::size_t colon = error.find(':'); colon != std::string::npos;
         colon = error.find(':', colon + 1)) {
        std::size_t begin = colon + 1;
        std::size_t end = begin;
        while (end < error.size()
               && std::isdigit(static_cast<unsigned char>(error[end]))) ++end;
        if (end > begin && end < error.size() && error[end] == ':') {
            const long value = std::strtol(error.substr(begin, end - begin).c_str(), nullptr, 10);
            return value > 0 ? static_cast<int>(value) : 0;
        }
    }
    return 0;
}

std::string extractMessage(const std::string& error) {
    for (std::size_t colon = error.find(':'); colon != std::string::npos;
         colon = error.find(':', colon + 1)) {
        std::size_t end = colon + 1;
        while (end < error.size()
               && std::isdigit(static_cast<unsigned char>(error[end]))) ++end;
        if (end > colon + 1 && end < error.size() && error[end] == ':') {
            std::size_t message = end + 1;
            while (message < error.size() && error[message] == ' ') ++message;
            return error.substr(message);
        }
    }
    return error.empty() ? "Lua syntax validation failed" : error;
}

} // namespace

std::vector<ScriptDiagnostic> validateScriptSyntax(
    const AssetId& scriptAssetId,
    const std::string& sourcePath,
    const std::string& source) {
    std::unique_ptr<lua_State, LuaStateCloser> state(luaL_newstate());
    if (!state) {
        return {{DiagnosticSeverity::Error, "SCRIPT_VALIDATOR_UNAVAILABLE",
                 scriptAssetId, sourcePath, 0, 0, std::nullopt, {},
                 "Could not create the Lua syntax validator"}};
    }

    // No luaL_openlibs and no lua_pcall: loading compiles the text chunk only.
    const std::string chunkName = "@" + sourcePath;
    const int result = luaL_loadbufferx(state.get(), source.data(), source.size(),
                                        chunkName.c_str(), "t");
    if (result == LUA_OK) return {};

    const char* luaError = lua_tostring(state.get(), -1);
    const std::string error = luaError ? luaError : "Lua syntax validation failed";
    const int line = extractLine(error);
    const char* code = result == LUA_ERRSYNTAX
        ? "SCRIPT_SYNTAX" : "SCRIPT_VALIDATOR_FAILED";
    return {{DiagnosticSeverity::Error, code, scriptAssetId,
             sourcePath, line, line > 0 ? 1 : 0, std::nullopt, {},
             extractMessage(error)}};
}

std::vector<ScriptDiagnostic> validateReferencedScriptSyntax(
    const ProjectDocument& document,
    const ProjectScriptFileService& files,
    const std::vector<AssetId>& referencedScriptAssetIds) {
    std::vector<ScriptDiagnostic> diagnostics;
    std::unordered_set<AssetId> visited;
    for (const AssetId& assetId : referencedScriptAssetIds) {
        if (!visited.insert(assetId).second) continue;
        const ScriptAssetDef* asset = document.findScriptAsset(assetId);
        if (!asset) {
            diagnostics.push_back({DiagnosticSeverity::Error,
                "SCRIPT_REFERENCE_UNKNOWN", assetId, {}, 0, 0,
                std::nullopt, {}, "Referenced script asset does not exist"});
            continue;
        }
        const ScriptFileResult<std::string> source = files.readScript(asset->sourcePath);
        if (!source.ok) {
            diagnostics.push_back({DiagnosticSeverity::Error,
                "SCRIPT_SOURCE_UNREADABLE", assetId, asset->sourcePath, 0, 0,
                std::nullopt, {}, source.error});
            continue;
        }
        std::vector<ScriptDiagnostic> sourceDiagnostics =
            validateScriptSyntax(assetId, asset->sourcePath, source.value);
        diagnostics.insert(diagnostics.end(),
                           std::make_move_iterator(sourceDiagnostics.begin()),
                           std::make_move_iterator(sourceDiagnostics.end()));
    }
    return diagnostics;
}

} // namespace ArtCade::EditorNative
