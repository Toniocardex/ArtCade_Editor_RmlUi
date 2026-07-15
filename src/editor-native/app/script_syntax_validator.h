#pragma once

#include "editor-native/model/script_editor_state.h"

#include <string>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;
class ProjectScriptFileService;

// Pure compile-only boundary. It creates an unopened Lua 5.4 state and loads
// text source without ever calling the resulting chunk.
std::vector<ScriptDiagnostic> validateScriptSyntax(
    const AssetId& scriptAssetId,
    const std::string& sourcePath,
    const std::string& source);

// Strict saved-source half of the Play boundary. The caller supplies the
// authored reference set; validating every catalog entry would incorrectly
// make unrelated drafts block Play.
std::vector<ScriptDiagnostic> validateReferencedScriptSyntax(
    const ProjectDocument& document,
    const ProjectScriptFileService& files,
    const std::vector<AssetId>& referencedScriptAssetIds);

} // namespace ArtCade::EditorNative
