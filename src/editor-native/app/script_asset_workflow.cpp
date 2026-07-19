#include "editor-native/app/script_asset_workflow.h"

#include "editor-native/app/editor_command_side_effect.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/project_script_file_service.h"
#include "editor-native/commands/script_asset_commands.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <system_error>
#include <utility>
#include <vector>

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

namespace {

std::uint64_t nextScriptDeleteToken() {
    static std::atomic<std::uint64_t> token{1};
    return token.fetch_add(1, std::memory_order_relaxed);
}

EditorCommandSideEffectResult asSideEffect(const ScriptFileResult<bool>& result) {
    return result.ok ? EditorCommandSideEffectResult::success()
                     : EditorCommandSideEffectResult::failure(result.error);
}

EditorCommandSideEffectResult asSideEffect(
    const ScriptFileResult<std::filesystem::path>& result) {
    return result.ok ? EditorCommandSideEffectResult::success()
                     : EditorCommandSideEffectResult::failure(result.error);
}

class DeletedScriptSourceSideEffect final : public EditorCommandSideEffect {
public:
    enum class State { Applied, Undone, Abandoned, RecoveryFailed };

    DeletedScriptSourceSideEffect(
        AssetId assetId,
        std::shared_ptr<ProjectScriptFileService> files,
        std::filesystem::path projectRoot,
        std::filesystem::path relativePath,
        std::filesystem::path finalPath,
        std::filesystem::path restoreStagingPath,
        bool fileExisted,
        std::vector<std::uint8_t> bytes,
        ScriptDirtyBufferQuery dirtyBufferQuery)
        : assetId_(std::move(assetId)),
          files_(std::move(files)),
          projectRoot_(std::move(projectRoot)),
          relativePath_(std::move(relativePath)),
          finalPath_(std::move(finalPath)),
          restoreStagingPath_(std::move(restoreStagingPath)),
          fileExisted_(fileExisted),
          bytes_(std::move(bytes)),
          dirtyBufferQuery_(std::move(dirtyBufferQuery)) {}

    ~DeletedScriptSourceSideEffect() override {
        if (files_) files_->removeAbsoluteIfExists(restoreStagingPath_);
    }

    EditorCommandSideEffectResult rollbackInitial() override {
        const auto restored = restoreDeletedFile();
        state_ = restored.ok ? State::Abandoned : State::RecoveryFailed;
        return restored;
    }

    EditorCommandSideEffectResult prepareUndo() override {
        if (state_ != State::Applied) {
            return EditorCommandSideEffectResult::failure(
                "Script deletion is not in the applied state");
        }
        return restoreDeletedFile();
    }

    EditorCommandSideEffectResult rollbackUndo() override {
        const auto removed = removeRestoredFile(false);
        if (!removed.ok) state_ = State::RecoveryFailed;
        return removed;
    }

    void commitUndo() override { state_ = State::Undone; }

    EditorCommandSideEffectResult prepareRedo() override {
        if (state_ != State::Undone) {
            return EditorCommandSideEffectResult::failure(
                "Script deletion is not in the undone state");
        }
        if (dirtyBufferQuery_ && dirtyBufferQuery_(assetId_)) {
            return EditorCommandSideEffectResult::failure(
                "Cannot redo Script deletion while the Script has unsaved changes. "
                "Save or close the Script first.");
        }
        return removeRestoredFile(true);
    }

    EditorCommandSideEffectResult rollbackRedo() override {
        const auto restored = restoreDeletedFile();
        if (!restored.ok) state_ = State::RecoveryFailed;
        return restored;
    }

    void commitRedo() override { state_ = State::Applied; }

    EditorCommandSideEffectResult validateProjectRootRebase(
        const std::filesystem::path& previousRoot,
        const std::filesystem::path& nextRoot) const override {
        if (state_ == State::RecoveryFailed) {
            return EditorCommandSideEffectResult::failure(
                "Script deletion history is awaiting filesystem recovery");
        }
        std::filesystem::path nextFinal;
        if (!rebasePath(finalPath_, previousRoot, nextRoot, nextFinal)) {
            return EditorCommandSideEffectResult::failure(
                "Script deletion path is outside the previous project root");
        }
        ProjectScriptFileService nextFiles{nextRoot};
        const auto file = nextFiles.readRawAbsoluteIfExists(nextFinal, true);
        if (!file.ok) {
            return EditorCommandSideEffectResult::failure(file.error);
        }
        if (state_ == State::Applied && file.value.existed) {
            return EditorCommandSideEffectResult::failure(
                "Save As copied a Script source that is deleted in the current project");
        }
        if (state_ == State::Undone
            && (file.value.existed != fileExisted_
                || (fileExisted_ && file.value.bytes != bytes_))) {
            return EditorCommandSideEffectResult::failure(
                "Save As did not preserve the exact restored Script source");
        }
        return EditorCommandSideEffectResult::success();
    }

    void rebaseProjectRoot(const std::filesystem::path& previousRoot,
                           const std::filesystem::path& nextRoot) override {
        std::filesystem::path nextFinal;
        std::filesystem::path nextStaging;
        if (rebasePath(finalPath_, previousRoot, nextRoot, nextFinal))
            finalPath_ = std::move(nextFinal);
        if (rebasePath(restoreStagingPath_, previousRoot, nextRoot, nextStaging))
            restoreStagingPath_ = std::move(nextStaging);
        projectRoot_ = nextRoot;
        if (files_) files_ = std::make_shared<ProjectScriptFileService>(projectRoot_);
    }

private:
    static bool rebasePath(const std::filesystem::path& path,
                           const std::filesystem::path& previousRoot,
                           const std::filesystem::path& nextRoot,
                           std::filesystem::path& result) {
        const std::filesystem::path relative = path.lexically_relative(previousRoot);
        if (relative.empty() || relative.is_absolute()
            || *relative.begin() == std::filesystem::path{".."}) return false;
        result = nextRoot / relative;
        return true;
    }

    EditorCommandSideEffectResult restoreDeletedFile() {
        if (!files_) {
            return EditorCommandSideEffectResult::failure(
                "Script file service is unavailable");
        }
        const auto current = files_->readRawAbsoluteIfExists(finalPath_, true);
        if (!current.ok) {
            return EditorCommandSideEffectResult::failure(current.error);
        }
        if (current.value.existed) {
            return EditorCommandSideEffectResult::failure(
                "Cannot restore Script source because its path already exists");
        }
        if (!fileExisted_) return EditorCommandSideEffectResult::success();

        files_->removeAbsoluteIfExists(restoreStagingPath_);
        const auto written = files_->writeRawAbsoluteNoReplace(
            restoreStagingPath_, bytes_, false);
        if (!written.ok) {
            return EditorCommandSideEffectResult::failure(written.error);
        }
        const auto restored = files_->moveAbsoluteNoReplace(
            restoreStagingPath_, finalPath_);
        if (!restored.ok) {
            files_->removeAbsoluteIfExists(restoreStagingPath_);
            return EditorCommandSideEffectResult::failure(restored.error);
        }
        return EditorCommandSideEffectResult::success();
    }

    EditorCommandSideEffectResult removeRestoredFile(bool requireExactMatch) {
        if (!files_) {
            return EditorCommandSideEffectResult::failure(
                "Script file service is unavailable");
        }
        const auto current = files_->readRawAbsoluteIfExists(finalPath_, true);
        if (!current.ok) {
            return EditorCommandSideEffectResult::failure(current.error);
        }
        if (requireExactMatch) {
            if (current.value.existed != fileExisted_
                || (fileExisted_ && current.value.bytes != bytes_)) {
                return EditorCommandSideEffectResult::failure(
                    "Script source changed outside ArtCade; refusing destructive Redo");
            }
        }
        if (!fileExisted_) return EditorCommandSideEffectResult::success();
        return asSideEffect(files_->removeAbsoluteIfExists(finalPath_));
    }

    AssetId assetId_;
    std::shared_ptr<ProjectScriptFileService> files_;
    std::filesystem::path projectRoot_;
    std::filesystem::path relativePath_;
    std::filesystem::path finalPath_;
    std::filesystem::path restoreStagingPath_;
    bool fileExisted_ = false;
    std::vector<std::uint8_t> bytes_;
    ScriptDirtyBufferQuery dirtyBufferQuery_;
    State state_ = State::Applied;
};

bool scriptAssetIsReferenced(const ProjectDocument& document, const AssetId& assetId) {
    const std::vector<AssetId> referenced = document.referencedScriptAssetIds(false);
    return std::find(referenced.begin(), referenced.end(), assetId) != referenced.end();
}

} // namespace

ScriptAssetWorkflowResult removeScriptAsset(
    EditorCoordinator& coordinator,
    const std::filesystem::path& projectRoot,
    const AssetId& assetId,
    ScriptDirtyBufferQuery dirtyBufferQuery) {
    if (coordinator.isPlaying()) {
        return ScriptAssetWorkflowResult::failure("Stop Play before deleting scripts");
    }
    if (projectRoot.empty() || !projectRoot.is_absolute()) {
        return ScriptAssetWorkflowResult::failure(
            "Save the project before deleting scripts");
    }
    const ScriptAssetDef* asset = coordinator.document().findScriptAsset(assetId);
    if (!asset) {
        return ScriptAssetWorkflowResult::failure("Unknown script asset: " + assetId);
    }
    if (scriptAssetIsReferenced(coordinator.document(), assetId)) {
        return ScriptAssetWorkflowResult::failure(
            "Cannot remove Script Asset while it is attached to an Object Type");
    }

    auto files = std::make_shared<ProjectScriptFileService>(projectRoot);
    const std::filesystem::path relative =
        std::filesystem::u8path(asset->sourcePath);
    const PathConfinementResult resolved = files->resolveProjectRelativePath(relative);
    if (!resolved.ok) {
        return ScriptAssetWorkflowResult::failure(
            resolved.error + ". " + resolved.remediation);
    }

    std::filesystem::path restoreStaging =
        resolved.value.parent_path()
        / (resolved.value.filename().string()
           + ".artcade-delete-" + std::to_string(nextScriptDeleteToken()) + ".tmp");

    const auto stagingProbe = files->readRawAbsoluteIfExists(restoreStaging, false);
    if (!stagingProbe.ok) {
        return ScriptAssetWorkflowResult::failure(stagingProbe.error);
    }
    if (stagingProbe.value.existed) {
        return ScriptAssetWorkflowResult::failure(
            "Script deletion recovery path already exists");
    }

    bool fileExisted = false;
    std::vector<std::uint8_t> preservedBytes;
    {
        const auto current = files->readRawAbsoluteIfExists(resolved.value, true);
        if (!current.ok) {
            return ScriptAssetWorkflowResult::failure(current.error);
        }
        if (current.value.existed) {
            const auto isolated = files->moveAbsoluteNoReplace(
                resolved.value, restoreStaging);
            if (!isolated.ok) {
                return ScriptAssetWorkflowResult::failure(
                    "Could not isolate Script source for deletion: " + isolated.error);
            }
            const auto preserved = files->readRawAbsoluteIfExists(restoreStaging, false);
            if (!preserved.ok || !preserved.value.existed) {
                const auto rollback = files->moveAbsoluteNoReplace(
                    restoreStaging, resolved.value);
                return ScriptAssetWorkflowResult::failure(
                    "Could not preserve Script source for Undo: "
                    + (preserved.ok ? std::string{"isolated file disappeared"}
                                    : preserved.error)
                    + (rollback.ok ? std::string{}
                                   : "; rollback failed: " + rollback.error));
            }
            const auto removed = files->removeAbsoluteIfExists(restoreStaging);
            if (!removed.ok) {
                const auto rollback = files->moveAbsoluteNoReplace(
                    restoreStaging, resolved.value);
                return ScriptAssetWorkflowResult::failure(
                    "Could not delete Script source: " + removed.error
                    + (rollback.ok ? std::string{}
                                   : "; rollback failed: " + rollback.error));
            }
            fileExisted = true;
            preservedBytes = std::move(preserved.value.bytes);
        }
    }

    auto sideEffect = std::make_unique<DeletedScriptSourceSideEffect>(
        assetId, files, projectRoot, relative, resolved.value, restoreStaging,
        fileExisted, std::move(preservedBytes), std::move(dirtyBufferQuery));

    const EditorOperationResult removed = coordinator.executeWithSideEffect(
        RemoveScriptAssetCommand{assetId}, std::move(sideEffect));
    if (!removed.ok) {
        return ScriptAssetWorkflowResult::failure(removed.error);
    }
    return ScriptAssetWorkflowResult::success(assetId);
}

} // namespace ArtCade::EditorNative
