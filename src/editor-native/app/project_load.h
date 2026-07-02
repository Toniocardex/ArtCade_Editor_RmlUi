#pragma once

#include "editor-native/commands/editor_operation_result.h"

#include <string>
#include <string_view>
#include <utility>

namespace ArtCade::EditorNative {

class EditorCoordinator;

enum class ProjectLoadStage {
    FileRead,
    Deserialize,
    Migration,
    Validation,
    Replace,
};

struct ProjectLoadError {
    ProjectLoadStage stage = ProjectLoadStage::Deserialize;
    std::string      message;
};

struct ProjectLoadResult {
    bool ok = false;
    EditorOperationResult operation;
    ProjectLoadError error;

    static ProjectLoadResult success(EditorOperationResult result) {
        ProjectLoadResult out;
        out.ok = true;
        out.operation = std::move(result);
        return out;
    }

    static ProjectLoadResult failure(ProjectLoadStage stage, std::string message) {
        ProjectLoadResult out;
        out.error = ProjectLoadError{stage, std::move(message)};
        return out;
    }
};

ProjectLoadResult loadProjectFromText(EditorCoordinator& coordinator,
                                      std::string_view source);

} // namespace ArtCade::EditorNative
