#pragma once

#include "editor-native/app/export/export_types.h"
#include "editor-native/model/project_document.h"
#include "logic-core.h"

#include <string>
#include <vector>

namespace ArtCade::EditorNative {

struct RuntimeProjectPreflightResult {
    bool ok = false;
    std::string canonicalProjectJson;
    Logic::LogicCompileResult compiledLogic;
    std::vector<ExportDiagnostic> diagnostics;
};

// Shared Play/Export domain preflight (ADR-0019). No script filesystem I/O.
RuntimeProjectPreflightResult prepareRuntimeProjectSnapshot(
    const ProjectDocument& document);

} // namespace ArtCade::EditorNative
