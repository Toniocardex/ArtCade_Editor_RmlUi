#include "editor-native/app/project_file.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/model/project_io.h"

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ArtCade::EditorNative {

namespace {

std::filesystem::path makeTempPath(const std::filesystem::path& destination) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path temp = destination;
    temp += ".tmp-" + std::to_string(ticks);
    return temp;
}

std::string errorMessage(const std::string& prefix, const std::error_code& ec) {
    return ec ? prefix + ": " + ec.message() : prefix;
}

bool replaceFileWithTemp(const std::filesystem::path& temp,
                         const std::filesystem::path& destination,
                         std::string& error) {
#if defined(_WIN32)
    if (!MoveFileExW(temp.wstring().c_str(), destination.wstring().c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "Could not replace destination file: Windows error "
              + std::to_string(GetLastError());
        return false;
    }
    return true;
#else
    std::error_code ec;
    std::filesystem::rename(temp, destination, ec);
    if (ec) {
        error = errorMessage("Could not replace destination file", ec);
        return false;
    }
    return true;
#endif
}

ProjectTextFileResult writeFileAtomically(const std::filesystem::path& destination,
                                          const std::string& text) {
    const std::filesystem::path temp = makeTempPath(destination);

    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return ProjectTextFileResult::failure(temp, "Could not open temporary file");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            std::error_code cleanupEc;
            std::filesystem::remove(temp, cleanupEc);
            return ProjectTextFileResult::failure(temp, "Could not write project file");
        }
    }

    std::string replaceError;
    if (!replaceFileWithTemp(temp, destination, replaceError)) {
        std::error_code cleanupEc;
        std::filesystem::remove(temp, cleanupEc);
        return ProjectTextFileResult::failure(destination, std::move(replaceError));
    }

    return ProjectTextFileResult::success({});
}

} // namespace

ProjectTextFileResult writeProjectTextFileAtomically(
    const std::filesystem::path& destination, const std::string& text) {
    return writeFileAtomically(destination, text);
}

ProjectTextFileResult readProjectTextFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return ProjectTextFileResult::failure(path, "Could not open project file");
    }

    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (!in.good() && !in.eof()) {
        return ProjectTextFileResult::failure(path, "Could not read project file");
    }
    return ProjectTextFileResult::success(buffer.str());
}

ProjectLoadResult loadProjectFromFile(EditorCoordinator& coordinator,
                                      const std::filesystem::path& path) {
    ProjectTextFileResult source = readProjectTextFile(path);
    if (!source.ok) {
        return ProjectLoadResult::failure(ProjectLoadStage::FileRead,
                                          source.error.message);
    }
    return loadProjectFromText(coordinator, source.value);
}

ProjectSaveResult saveProjectToFile(EditorCoordinator& coordinator,
                                    const std::filesystem::path& destination) {
    ProjectSaveResult persisted =
        saveProjectDocumentToFile(coordinator.document(), destination);
    if (!persisted.ok) return persisted;

    EditorOperationResult saved = coordinator.markProjectSaved();
    if (!saved.ok) {
        return ProjectSaveResult::failure(ProjectSaveStage::MarkSaved,
                                          destination, saved.error);
    }
    return ProjectSaveResult::success(std::move(saved));
}

ProjectSaveResult saveProjectDocumentToFile(
    const ProjectDocument& candidate, const std::filesystem::path& destination) {
    DeserializeResult validated;
    try {
        validated = ProjectValidator::validate(ProjectDocument{candidate.data()});
    } catch (const std::exception& error) {
        return ProjectSaveResult::failure(
            ProjectSaveStage::Validation, destination,
            std::string("Project validation failed: ") + error.what());
    }
    if (!validated.ok) {
        return ProjectSaveResult::failure(ProjectSaveStage::Validation,
                                          destination, std::move(validated.error));
    }

    SerializeResult serialized;
    try {
        serialized = ProjectSerializer::serialize(candidate);
    } catch (const std::exception& error) {
        return ProjectSaveResult::failure(
            ProjectSaveStage::Serialize, destination,
            std::string("Project serialization failed: ") + error.what());
    }
    if (!serialized.ok) {
        return ProjectSaveResult::failure(ProjectSaveStage::Serialize,
                                          destination, std::move(serialized.error));
    }

    ProjectTextFileResult written =
        writeProjectTextFileAtomically(destination, serialized.value);
    if (!written.ok) {
        return ProjectSaveResult::failure(ProjectSaveStage::FileWrite,
                                          written.error.path,
                                          std::move(written.error.message));
    }

    return ProjectSaveResult::success(
        EditorOperationResult::success(EditorInvalidation::None));
}

} // namespace ArtCade::EditorNative
