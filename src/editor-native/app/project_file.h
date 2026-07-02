#pragma once

#include "editor-native/app/project_load.h"

#include <filesystem>
#include <string>
#include <utility>

namespace ArtCade::EditorNative {

class EditorCoordinator;

struct ProjectFileError {
    std::filesystem::path path;
    std::string           message;
};

struct ProjectTextFileResult {
    bool             ok = false;
    std::string      value;
    ProjectFileError error;

    static ProjectTextFileResult success(std::string text) {
        ProjectTextFileResult out;
        out.ok = true;
        out.value = std::move(text);
        return out;
    }

    static ProjectTextFileResult failure(std::filesystem::path path, std::string message) {
        ProjectTextFileResult out;
        out.error = ProjectFileError{std::move(path), std::move(message)};
        return out;
    }
};

enum class ProjectSaveStage {
    Validation,
    Serialize,
    FileWrite,
    MarkSaved,
};

struct ProjectSaveError {
    ProjectSaveStage      stage = ProjectSaveStage::Validation;
    std::filesystem::path path;
    std::string           message;
};

struct ProjectSaveResult {
    bool ok = false;
    EditorOperationResult operation;
    ProjectSaveError error;

    static ProjectSaveResult success(EditorOperationResult result) {
        ProjectSaveResult out;
        out.ok = true;
        out.operation = std::move(result);
        return out;
    }

    static ProjectSaveResult failure(ProjectSaveStage stage,
                                     std::filesystem::path path,
                                     std::string message) {
        ProjectSaveResult out;
        out.error = ProjectSaveError{stage, std::move(path), std::move(message)};
        return out;
    }
};

ProjectTextFileResult readProjectTextFile(const std::filesystem::path& path);
ProjectLoadResult loadProjectFromFile(EditorCoordinator& coordinator,
                                      const std::filesystem::path& path);
ProjectSaveResult saveProjectToFile(EditorCoordinator& coordinator,
                                    const std::filesystem::path& destination);

} // namespace ArtCade::EditorNative
