#include "editor-native/app/script_asset_workflow.h"

#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/project_script_file_service.h"
#include "editor-native/commands/script_asset_commands.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace ArtCade::EditorNative {

namespace {

struct ScriptDestination {
    AssetId assetId;
    std::string name;
    std::filesystem::path relativePath;
};

std::string safeStem(std::string value) {
    for (char& c : value) {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (!(std::isalnum(byte) || c == '-' || c == '_')) c = '_';
    }
    while (!value.empty() && value.front() == '_') value.erase(value.begin());
    while (!value.empty() && value.back() == '_') value.pop_back();
    return value.empty() ? std::string("Script") : value;
}

ScriptFileResult<ScriptDestination> chooseDestination(
    const EditorCoordinator& coordinator, const ProjectScriptFileService& files,
    std::string requestedStem) {
    const std::string stem = safeStem(std::move(requestedStem));
    for (int suffix = 1; suffix < 100000; ++suffix) {
        const std::string candidate = suffix == 1
            ? stem : stem + "_" + std::to_string(suffix);
        const std::filesystem::path relative =
            std::filesystem::u8path("scripts") / (candidate + ".lua");
        const PathConfinementResult resolved = files.resolveProjectRelativePath(relative);
        if (!resolved.ok) {
            return ScriptFileResult<ScriptDestination>::failure(
                resolved.error + ". " + resolved.remediation);
        }
        std::error_code ec;
        const bool exists = std::filesystem::exists(resolved.value, ec);
        if (ec) {
            return ScriptFileResult<ScriptDestination>::failure(
                "Could not inspect script destination: " + ec.message());
        }
        if (!exists && !coordinator.document().hasScriptAsset(candidate)) {
            return ScriptFileResult<ScriptDestination>::success(
                ScriptDestination{candidate, candidate, relative});
        }
    }
    return ScriptFileResult<ScriptDestination>::failure(
        "Could not allocate a unique script asset id");
}

ScriptAssetWorkflowResult persistAndRegister(
    EditorCoordinator& coordinator, const ProjectScriptFileService& files,
    const ScriptDestination& destination, std::string source) {
    const auto written = files.writeScriptAtomically(destination.relativePath, std::move(source));
    if (!written.ok) return ScriptAssetWorkflowResult::failure(written.error);
    const EditorOperationResult added = coordinator.execute(AddScriptAssetCommand{
        destination.assetId, destination.name, destination.relativePath.generic_u8string()});
    if (!added.ok) {
        std::error_code ec;
        std::filesystem::remove(written.value, ec);
        std::string error = "Script registration failed: " + added.error;
        if (ec) error += "; cleanup also failed: " + ec.message();
        return ScriptAssetWorkflowResult::failure(std::move(error));
    }
    return ScriptAssetWorkflowResult::success(destination.assetId);
}

} // namespace

ScriptAssetWorkflowResult createScriptAsset(
    EditorCoordinator& coordinator, const std::filesystem::path& projectRoot,
    std::string baseName) {
    if (coordinator.isPlaying()) {
        return ScriptAssetWorkflowResult::failure("Stop Play before creating scripts");
    }
    if (projectRoot.empty()) {
        return ScriptAssetWorkflowResult::failure("Save the project before creating scripts");
    }
    ProjectScriptFileService files{projectRoot};
    const auto destination = chooseDestination(coordinator, files, std::move(baseName));
    if (!destination.ok) return ScriptAssetWorkflowResult::failure(destination.error);
    static const std::string kTemplate =
        "artcade.require_api_version(1)\n\n"
        "return {\n"
        "    on_start = function(ctx)\n"
        "    end,\n"
        "}\n";
    return persistAndRegister(coordinator, files, destination.value, kTemplate);
}

ScriptAssetWorkflowResult importScriptAsset(
    EditorCoordinator& coordinator, const std::filesystem::path& projectRoot,
    const std::filesystem::path& sourcePath) {
    if (coordinator.isPlaying()) {
        return ScriptAssetWorkflowResult::failure("Stop Play before importing scripts");
    }
    if (projectRoot.empty()) {
        return ScriptAssetWorkflowResult::failure("Save the project before importing scripts");
    }
    ProjectScriptFileService files{projectRoot};
    const auto source = files.readImportSource(sourcePath);
    if (!source.ok) return ScriptAssetWorkflowResult::failure(source.error);
    const auto destination = chooseDestination(coordinator, files, sourcePath.stem().string());
    if (!destination.ok) return ScriptAssetWorkflowResult::failure(destination.error);
    return persistAndRegister(coordinator, files, destination.value, source.value);
}

} // namespace ArtCade::EditorNative
