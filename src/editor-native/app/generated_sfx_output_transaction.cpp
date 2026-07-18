#include "editor-native/app/generated_sfx_output_transaction.h"

#include "editor-native/app/editor_command_side_effect.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/commands/editor_intent.h"
#include "editor-native/commands/generated_sfx_commands.h"
#include "editor-native/model/generated_sfx_policy.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <fstream>
#include <limits>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
extern "C" __declspec(dllimport) int __stdcall MoveFileExW(
    const wchar_t* existing, const wchar_t* replacement, unsigned long flags);
extern "C" __declspec(dllimport) unsigned long __stdcall GetLastError();
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace ArtCade::EditorNative {

namespace {

constexpr std::uintmax_t kMaximumGeneratedSfxHistoryBytes = 32u * 1024u * 1024u;

std::uint64_t nextDeletionToken() {
    static std::atomic<std::uint64_t> token{1};
    return token.fetch_add(1, std::memory_order_relaxed);
}

std::filesystem::path backupPath(const std::filesystem::path& finalPath,
                                 const char* role,
                                 std::uint64_t token) {
    return finalPath.parent_path()
        / (finalPath.filename().string() + ".artcade-" + role + "-"
           + std::to_string(token) + ".tmp");
}

EditorCommandSideEffectResult asSideEffect(
    GeneratedSfxFileOperationResult result) {
    return result.ok ? EditorCommandSideEffectResult::success()
                     : EditorCommandSideEffectResult::failure(std::move(result.error));
}

class GeneratedSfxArtifactSideEffect final : public EditorCommandSideEffect {
public:
    enum class State { Applied, Undone, Abandoned, RecoveryFailed };

    GeneratedSfxArtifactSideEffect(
        std::shared_ptr<GeneratedSfxOutputRepository> repository,
        std::filesystem::path finalPath,
        std::filesystem::path previousBackup,
        std::filesystem::path generatedBackup)
        : repository_(std::move(repository)),
          finalPath_(std::move(finalPath)),
          previousBackup_(std::move(previousBackup)),
          generatedBackup_(std::move(generatedBackup)) {}

    ~GeneratedSfxArtifactSideEffect() override {
        // Applied: the current WAV stays, the superseded one is no longer
        // reachable. Undone: the previous/missing state stays, while the Redo
        // branch's generated WAV is discarded when history drops that branch.
        if (state_ == State::Applied) {
            if (!previousBackup_.empty()) repository_->removeIfExists(previousBackup_);
            repository_->removeIfExists(generatedBackup_);
        } else if (state_ == State::Undone) {
            repository_->removeIfExists(generatedBackup_);
            if (!previousBackup_.empty()) repository_->removeIfExists(previousBackup_);
        } else if (state_ == State::Abandoned) {
            repository_->removeIfExists(generatedBackup_);
            if (!previousBackup_.empty()) repository_->removeIfExists(previousBackup_);
        }
    }

    EditorCommandSideEffectResult rollbackInitial() override {
        const auto removed = repository_->removeIfExists(finalPath_);
        if (!removed.ok) {
            state_ = State::RecoveryFailed;
            return asSideEffect(removed);
        }
        if (!previousBackup_.empty()) {
            const auto restored = repository_->moveNoReplace(previousBackup_, finalPath_);
            if (!restored.ok) {
                state_ = State::RecoveryFailed;
                return asSideEffect(restored);
            }
        }
        state_ = State::Abandoned;
        return EditorCommandSideEffectResult::success();
    }

    EditorCommandSideEffectResult prepareUndo() override {
        if (state_ != State::Applied)
            return EditorCommandSideEffectResult::failure(
                "Generated SFX artifact is not in the applied state");
        auto movedNew = repository_->moveNoReplace(finalPath_, generatedBackup_);
        if (!movedNew.ok) return asSideEffect(std::move(movedNew));
        if (!previousBackup_.empty()) {
            auto restoredOld = repository_->moveNoReplace(previousBackup_, finalPath_);
            if (!restoredOld.ok) {
                const auto rollback = repository_->moveNoReplace(
                    generatedBackup_, finalPath_);
                if (!rollback.ok) state_ = State::RecoveryFailed;
                return EditorCommandSideEffectResult::failure(
                    restoredOld.error + (rollback.ok ? std::string{}
                        : "; rollback failed: " + rollback.error));
            }
        }
        return EditorCommandSideEffectResult::success();
    }

    EditorCommandSideEffectResult rollbackUndo() override {
        if (!previousBackup_.empty()) {
            auto movedOld = repository_->moveNoReplace(finalPath_, previousBackup_);
            if (!movedOld.ok) {
                state_ = State::RecoveryFailed;
                return asSideEffect(std::move(movedOld));
            }
        }
        auto restored = repository_->moveNoReplace(generatedBackup_, finalPath_);
        if (!restored.ok) state_ = State::RecoveryFailed;
        return asSideEffect(std::move(restored));
    }

    void commitUndo() override { state_ = State::Undone; }

    EditorCommandSideEffectResult prepareRedo() override {
        if (state_ != State::Undone)
            return EditorCommandSideEffectResult::failure(
                "Generated SFX artifact is not in the undone state");
        if (!previousBackup_.empty()) {
            auto movedOld = repository_->moveNoReplace(finalPath_, previousBackup_);
            if (!movedOld.ok) return asSideEffect(std::move(movedOld));
        }
        auto restoredNew = repository_->moveNoReplace(generatedBackup_, finalPath_);
        if (!restoredNew.ok) {
            if (!previousBackup_.empty()) {
                const auto rollback = repository_->moveNoReplace(
                    previousBackup_, finalPath_);
                if (!rollback.ok) {
                    state_ = State::RecoveryFailed;
                    restoredNew.error += "; rollback failed: " + rollback.error;
                }
            }
            return asSideEffect(std::move(restoredNew));
        }
        return EditorCommandSideEffectResult::success();
    }

    EditorCommandSideEffectResult rollbackRedo() override {
        auto movedNew = repository_->moveNoReplace(finalPath_, generatedBackup_);
        if (!movedNew.ok) {
            state_ = State::RecoveryFailed;
            return asSideEffect(std::move(movedNew));
        }
        if (!previousBackup_.empty()) {
            auto restored = repository_->moveNoReplace(previousBackup_, finalPath_);
            if (!restored.ok) state_ = State::RecoveryFailed;
            return asSideEffect(std::move(restored));
        }
        return EditorCommandSideEffectResult::success();
    }

    void commitRedo() override { state_ = State::Applied; }

    EditorCommandSideEffectResult validateProjectRootRebase(
        const std::filesystem::path& previousRoot,
        const std::filesystem::path& nextRoot) const override {
        if (state_ == State::RecoveryFailed)
            return EditorCommandSideEffectResult::failure(
                "Generated SFX history is awaiting filesystem recovery");

        const auto rebase = [&](const std::filesystem::path& path,
                                std::filesystem::path& rebased) {
            if (path.empty()) {
                rebased.clear();
                return true;
            }
            const std::filesystem::path relative = path.lexically_relative(previousRoot);
            if (relative.empty() || relative.is_absolute()
                || *relative.begin() == std::filesystem::path{".."}) return false;
            rebased = nextRoot / relative;
            return true;
        };

        std::filesystem::path nextFinal;
        std::filesystem::path nextPrevious;
        std::filesystem::path nextGenerated;
        if (!rebase(finalPath_, nextFinal)
            || !rebase(previousBackup_, nextPrevious)
            || !rebase(generatedBackup_, nextGenerated)) {
            return EditorCommandSideEffectResult::failure(
                "Generated SFX history path is outside the previous project root");
        }

        const auto requiredFinal = repository_->inspect(nextFinal);
        if (!requiredFinal.ok)
            return EditorCommandSideEffectResult::failure(requiredFinal.error);
        if (state_ == State::Applied && !requiredFinal.exists)
            return EditorCommandSideEffectResult::failure(
                "Save As did not copy the current Generated SFX WAV");
        if (state_ == State::Applied && !nextPrevious.empty()) {
            const auto requiredPrevious = repository_->inspect(nextPrevious);
            if (!requiredPrevious.ok || !requiredPrevious.exists)
                return EditorCommandSideEffectResult::failure(requiredPrevious.ok
                    ? "Save As did not copy the Generated SFX Undo backup"
                    : requiredPrevious.error);
        }
        if (state_ == State::Undone) {
            const auto requiredGenerated = repository_->inspect(nextGenerated);
            if (!requiredGenerated.ok || !requiredGenerated.exists)
                return EditorCommandSideEffectResult::failure(requiredGenerated.ok
                    ? "Save As did not copy the Generated SFX Redo backup"
                    : requiredGenerated.error);
        }

        return EditorCommandSideEffectResult::success();
    }

    void rebaseProjectRoot(const std::filesystem::path& previousRoot,
                           const std::filesystem::path& nextRoot) override {
        const auto rebase = [&](const std::filesystem::path& path) {
            if (path.empty()) return std::filesystem::path{};
            return nextRoot / path.lexically_relative(previousRoot);
        };
        const std::filesystem::path nextFinal = rebase(finalPath_);
        const std::filesystem::path nextPrevious = rebase(previousBackup_);
        const std::filesystem::path nextGenerated = rebase(generatedBackup_);

        // The old root becomes an independent saved snapshot. These history
        // backups are no longer reachable there; cleanup is best-effort and
        // cannot invalidate the already validated root transition.
        const auto clean = [&](const std::filesystem::path& path) {
            if (path.empty()) return;
            repository_->removeIfExists(path);
        };
        clean(previousBackup_);
        clean(generatedBackup_);
        finalPath_ = nextFinal;
        previousBackup_ = nextPrevious;
        generatedBackup_ = nextGenerated;
    }

private:
    std::shared_ptr<GeneratedSfxOutputRepository> repository_;
    std::filesystem::path finalPath_;
    std::filesystem::path previousBackup_;
    std::filesystem::path generatedBackup_;
    State state_ = State::Applied;
};

class DeletedGeneratedSfxArtifactSideEffect final
    : public EditorCommandSideEffect {
public:
    enum class State { Applied, Undone, Abandoned, RecoveryFailed };

    DeletedGeneratedSfxArtifactSideEffect(
        std::shared_ptr<GeneratedSfxOutputRepository> repository,
        std::filesystem::path finalPath,
        std::filesystem::path restoreStagingPath,
        bool fileExisted,
        std::vector<std::uint8_t> bytes)
        : repository_(std::move(repository)),
          finalPath_(std::move(finalPath)),
          restoreStagingPath_(std::move(restoreStagingPath)),
          fileExisted_(fileExisted),
          bytes_(std::move(bytes)) {}

    ~DeletedGeneratedSfxArtifactSideEffect() override {
        repository_->removeIfExists(restoreStagingPath_);
    }

    EditorCommandSideEffectResult rollbackInitial() override {
        const auto restored = restoreDeletedFile();
        state_ = restored.ok ? State::Abandoned : State::RecoveryFailed;
        return restored;
    }

    EditorCommandSideEffectResult prepareUndo() override {
        if (state_ != State::Applied)
            return EditorCommandSideEffectResult::failure(
                "Generated SFX deletion is not in the applied state");
        return restoreDeletedFile();
    }

    EditorCommandSideEffectResult rollbackUndo() override {
        const auto removed = removeRestoredFile();
        if (!removed.ok) state_ = State::RecoveryFailed;
        return removed;
    }

    void commitUndo() override { state_ = State::Undone; }

    EditorCommandSideEffectResult prepareRedo() override {
        if (state_ != State::Undone)
            return EditorCommandSideEffectResult::failure(
                "Generated SFX deletion is not in the undone state");
        return removeRestoredFile();
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
        if (state_ == State::RecoveryFailed)
            return EditorCommandSideEffectResult::failure(
                "Generated SFX deletion history is awaiting filesystem recovery");
        std::filesystem::path nextFinal;
        if (!rebasePath(finalPath_, previousRoot, nextRoot, nextFinal))
            return EditorCommandSideEffectResult::failure(
                "Generated SFX deletion path is outside the previous project root");
        const auto file = repository_->readIfExists(
            nextFinal, kMaximumGeneratedSfxHistoryBytes);
        if (!file.ok) return EditorCommandSideEffectResult::failure(file.error);
        if (state_ == State::Applied && file.exists)
            return EditorCommandSideEffectResult::failure(
                "Save As copied a WAV that is deleted in the current project");
        if (state_ == State::Undone
            && (file.exists != fileExisted_
                || (fileExisted_ && file.bytes != bytes_))) {
            return EditorCommandSideEffectResult::failure(
                "Save As did not preserve the exact restored Generated SFX WAV");
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
        const auto current = repository_->inspect(finalPath_);
        if (!current.ok) return EditorCommandSideEffectResult::failure(current.error);
        if (current.exists)
            return EditorCommandSideEffectResult::failure(
                "Cannot restore Generated SFX WAV because its path already exists");
        if (!fileExisted_) return EditorCommandSideEffectResult::success();
        repository_->removeIfExists(restoreStagingPath_);
        const auto written = repository_->writeNoReplace(
            restoreStagingPath_, bytes_);
        if (!written.ok)
            return EditorCommandSideEffectResult::failure(written.error);
        const auto restored = repository_->moveNoReplace(
            restoreStagingPath_, finalPath_);
        if (!restored.ok) {
            repository_->removeIfExists(restoreStagingPath_);
            return EditorCommandSideEffectResult::failure(restored.error);
        }
        return EditorCommandSideEffectResult::success();
    }

    EditorCommandSideEffectResult removeRestoredFile() {
        const auto current = repository_->readIfExists(
            finalPath_, kMaximumGeneratedSfxHistoryBytes);
        if (!current.ok) return EditorCommandSideEffectResult::failure(current.error);
        if (current.exists != fileExisted_
            || (fileExisted_ && current.bytes != bytes_)) {
            return EditorCommandSideEffectResult::failure(
                "Generated SFX WAV changed outside ArtCade; refusing destructive Redo");
        }
        if (!fileExisted_) return EditorCommandSideEffectResult::success();
        const auto removed = repository_->removeIfExists(finalPath_);
        return asSideEffect(removed);
    }

    std::shared_ptr<GeneratedSfxOutputRepository> repository_;
    std::filesystem::path finalPath_;
    std::filesystem::path restoreStagingPath_;
    bool fileExisted_ = false;
    std::vector<std::uint8_t> bytes_;
    State state_ = State::Applied;
};

} // namespace

GeneratedSfxFileInspection FilesystemGeneratedSfxOutputRepository::inspect(
    const std::filesystem::path& path) const {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) return {false, false, false, error.message()};
    if (!exists) return {true, false, false, {}};
    std::ifstream input{path, std::ios::binary};
    return {true, true, input.is_open(),
            input.is_open() ? std::string{} : "File is not readable"};
}

GeneratedSfxFileOperationResult
FilesystemGeneratedSfxOutputRepository::moveNoReplace(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    const auto destinationState = inspect(destination);
    if (!destinationState.ok)
        return GeneratedSfxFileOperationResult::failure(destinationState.error);
    if (destinationState.exists)
        return GeneratedSfxFileOperationResult::failure(
            "Destination already exists: " + destination.string());
#if defined(_WIN32)
    constexpr unsigned long kWriteThrough = 0x8u;
    if (MoveFileExW(source.c_str(), destination.c_str(), kWriteThrough))
        return GeneratedSfxFileOperationResult::success();
    return GeneratedSfxFileOperationResult::failure(
        std::system_category().message(static_cast<int>(GetLastError())));
#else
    // create_hard_link is the C++17 no-replace primitive for same-volume files:
    // destination creation is atomic and fails if another writer won the race.
    std::error_code linkError;
    std::filesystem::create_hard_link(source, destination, linkError);
    if (linkError)
        return GeneratedSfxFileOperationResult::failure(linkError.message());
    std::error_code removeError;
    std::filesystem::remove(source, removeError);
    if (!removeError) return GeneratedSfxFileOperationResult::success();
    std::error_code rollbackError;
    std::filesystem::remove(destination, rollbackError);
    return GeneratedSfxFileOperationResult::failure(
        removeError.message() + (rollbackError ? "; rollback failed: "
            + rollbackError.message() : std::string{}));
#endif
}

GeneratedSfxFileOperationResult
FilesystemGeneratedSfxOutputRepository::removeIfExists(
    const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::remove(path, error);
    return error ? GeneratedSfxFileOperationResult::failure(error.message())
                 : GeneratedSfxFileOperationResult::success();
}

GeneratedSfxFileReadResult
FilesystemGeneratedSfxOutputRepository::readIfExists(
    const std::filesystem::path& path,
    std::uintmax_t maximumBytes) const {
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (error) return GeneratedSfxFileReadResult::failure(error.message());
    if (!exists) return GeneratedSfxFileReadResult::missing();
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) return GeneratedSfxFileReadResult::failure(error.message());
    if (size > maximumBytes)
        return GeneratedSfxFileReadResult::failure(
            "Generated SFX WAV exceeds the Undo safety limit");
    std::ifstream input{path, std::ios::binary};
    if (!input.is_open())
        return GeneratedSfxFileReadResult::failure("File is not readable");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        input.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(bytes.size()))
            return GeneratedSfxFileReadResult::failure(
                "Could not read the complete Generated SFX WAV");
    }
    return GeneratedSfxFileReadResult::success(std::move(bytes));
}

GeneratedSfxFileOperationResult
FilesystemGeneratedSfxOutputRepository::writeNoReplace(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
#if defined(_WIN32)
    int descriptor = -1;
    const errno_t opened = _wsopen_s(
        &descriptor, path.c_str(),
        _O_BINARY | _O_WRONLY | _O_CREAT | _O_EXCL,
        _SH_DENYRW, _S_IREAD | _S_IWRITE);
    if (opened != 0)
        return GeneratedSfxFileOperationResult::failure(
            std::generic_category().message(static_cast<int>(opened)));
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const unsigned int count = static_cast<unsigned int>(std::min<std::size_t>(
            bytes.size() - offset,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int written = _write(descriptor, bytes.data() + offset, count);
        if (written <= 0) {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (ok && _commit(descriptor) != 0) ok = false;
    if (_close(descriptor) != 0) ok = false;
#else
    const int descriptor = ::open(
        path.c_str(), O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR);
    if (descriptor < 0)
        return GeneratedSfxFileOperationResult::failure(
            std::generic_category().message(errno));
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(
            descriptor, bytes.data() + offset, bytes.size() - offset);
        if (written <= 0) {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    if (ok && ::fsync(descriptor) != 0) ok = false;
    if (::close(descriptor) != 0) ok = false;
#endif
    if (ok) return GeneratedSfxFileOperationResult::success();
    std::error_code cleanup;
    std::filesystem::remove(path, cleanup);
    return GeneratedSfxFileOperationResult::failure(
        "Could not write the complete Generated SFX recovery file");
}

GeneratedSfxOutputTransaction::GeneratedSfxOutputTransaction(
    EditorCoordinator& coordinator,
    std::shared_ptr<GeneratedSfxOutputRepository> repository)
    : coordinator_(coordinator), repository_(std::move(repository)) {}

GeneratedSfxOutputCommitResult GeneratedSfxOutputTransaction::commit(
    const std::string& generatedSfxId,
    const artcade::sfx::SfxRecipe& renderedRecipe,
    const GeneratedSfxOutputIdentity& output,
    bool regenerating,
    std::uint64_t generationToken) {
    if (!repository_) return GeneratedSfxOutputCommitResult::failed(
        "Generated SFX output repository is unavailable");
    const auto* current = coordinator_.document().findGeneratedSfx(generatedSfxId);
    if (!current) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Generated SFX no longer exists: " + generatedSfxId);
    }
    if (!generatedSfxRecipesEqual(current->recipe, renderedRecipe)) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Generated SFX changed before output commit");
    }
    const bool documentOwnsOutput = !current->outputAssetId.empty()
        && current->outputAssetId == output.assetId
        && coordinator_.document().hasAudioAsset(output.assetId);
    if (regenerating != documentOwnsOutput) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Generated SFX output disposition no longer matches the document");
    }
    if (output.assetId != generatedAudioAssetId(generatedSfxId)
        || output.relativePath != generatedAudioRelativePath(generatedSfxId)
        || output.finalPath.empty() || output.stagingPath.empty()) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Generated SFX output identity is not canonical");
    }
    if (!regenerating
        && (coordinator_.document().hasAudioAsset(output.assetId)
            || generatedSfxOutputPathTaken(
                coordinator_.document(), output.relativePath))) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Generated SFX output id or path already exists",
            GeneratedSfxOutputCommitFailure::Collision);
    }

    const auto destination = repository_->inspect(output.finalPath);
    if (!destination.ok) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Could not inspect Generated SFX destination: " + destination.error);
    }
    if (!regenerating && destination.exists) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Generated audio destination already exists",
            GeneratedSfxOutputCommitFailure::Collision);
    }

    std::filesystem::path previousBackup;
    const std::filesystem::path generatedBackup =
        backupPath(output.finalPath, "redo", generationToken);
    const auto redoState = repository_->inspect(generatedBackup);
    if (!redoState.ok || redoState.exists) {
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(redoState.ok
            ? "Generated SFX Redo backup already exists" : redoState.error);
    }

    if (regenerating && destination.exists) {
        previousBackup = backupPath(output.finalPath, "previous", generationToken);
        const auto previousState = repository_->inspect(previousBackup);
        if (!previousState.ok || previousState.exists) {
            repository_->removeIfExists(output.stagingPath);
            return GeneratedSfxOutputCommitResult::failed(previousState.ok
                ? "Generated SFX Undo backup already exists" : previousState.error);
        }
        const auto preserved = repository_->moveNoReplace(
            output.finalPath, previousBackup);
        if (!preserved.ok) {
            repository_->removeIfExists(output.stagingPath);
            return GeneratedSfxOutputCommitResult::failed(
                "Could not preserve previous Generated SFX WAV: " + preserved.error);
        }
    }

    const auto finalized = repository_->moveNoReplace(
        output.stagingPath, output.finalPath);
    if (!finalized.ok) {
        const auto racedDestination = repository_->inspect(output.finalPath);
        std::string restoreError;
        if (!previousBackup.empty()) {
            const auto restored = repository_->moveNoReplace(
                previousBackup, output.finalPath);
            if (!restored.ok) restoreError = "; rollback failed: " + restored.error;
        }
        repository_->removeIfExists(output.stagingPath);
        return GeneratedSfxOutputCommitResult::failed(
            "Could not finalize Generated SFX WAV: " + finalized.error + restoreError,
            !regenerating && racedDestination.ok && racedDestination.exists
                ? GeneratedSfxOutputCommitFailure::Collision
                : GeneratedSfxOutputCommitFailure::Failed);
    }

    auto sideEffect = std::make_unique<GeneratedSfxArtifactSideEffect>(
        repository_, output.finalPath, previousBackup, generatedBackup);
    AudioAssetDef audio;
    audio.assetId = output.assetId;
    audio.name.clear();
    audio.sourcePath = output.relativePath;
    audio.loadMode = AudioLoadMode::StaticSound;
    const auto registered = coordinator_.executeWithSideEffect(
        RegisterGeneratedSfxOutputCommand{
            generatedSfxId, renderedRecipe, std::move(audio)},
        std::move(sideEffect));
    return registered.ok ? GeneratedSfxOutputCommitResult::success()
                         : GeneratedSfxOutputCommitResult::failed(registered.error);
}

EditorOperationResult GeneratedSfxOutputTransaction::remove(
    const RemoveGeneratedSfxIntent& intent,
    const std::filesystem::path& projectRoot) {
    if (!repository_)
        return EditorOperationResult::failure(
            "Generated SFX output repository is unavailable");
    if (coordinator_.isPlaying())
        return EditorOperationResult::failure(
            "Stop Play before deleting Generated SFX");
    const auto* definition = coordinator_.document().findGeneratedSfx(intent.id);
    if (!definition)
        return EditorOperationResult::failure(
            "Unknown Generated SFX: " + intent.id);

    std::unique_ptr<EditorCommandSideEffect> sideEffect;
    if (!definition->outputAssetId.empty()) {
        if (projectRoot.empty())
            return EditorOperationResult::failure(
                "Save the project before deleting its generated WAV");
        const auto identity = stableGeneratedSfxOutputIdentity(
            coordinator_.document(), *definition, projectRoot);
        if (!identity || identity->finalPath.empty())
            return EditorOperationResult::failure(
                "Generated SFX deletion could not resolve a canonical confined WAV path");

        const std::filesystem::path restoreStaging =
            identity->finalPath.parent_path()
            / (identity->finalPath.filename().string() + ".artcade-restore-"
               + std::to_string(nextDeletionToken()) + ".tmp");
        const auto staging = repository_->inspect(restoreStaging);
        if (!staging.ok || staging.exists)
            return EditorOperationResult::failure(staging.ok
                ? "Generated SFX recovery path already exists"
                : staging.error);

        const auto finalState = repository_->inspect(identity->finalPath);
        if (!finalState.ok)
            return EditorOperationResult::failure(
                "Could not inspect Generated SFX WAV: " + finalState.error);

        GeneratedSfxFileReadResult preserved = GeneratedSfxFileReadResult::missing();
        if (finalState.exists) {
            // Move first, then read: the bytes held for Undo are exactly the
            // bytes removed, even if another process was touching the old path.
            const auto isolated = repository_->moveNoReplace(
                identity->finalPath, restoreStaging);
            if (!isolated.ok)
                return EditorOperationResult::failure(
                    "Could not isolate Generated SFX WAV for deletion: "
                    + isolated.error);
            preserved = repository_->readIfExists(
                restoreStaging, kMaximumGeneratedSfxHistoryBytes);
            if (!preserved.ok || !preserved.exists) {
                const auto rollback = repository_->moveNoReplace(
                    restoreStaging, identity->finalPath);
                return EditorOperationResult::failure(
                    "Could not preserve Generated SFX WAV for Undo: "
                    + (preserved.ok ? std::string{"isolated WAV disappeared"}
                                    : preserved.error)
                    + (rollback.ok ? std::string{}
                                   : "; rollback failed: " + rollback.error));
            }
            const auto removed = repository_->removeIfExists(restoreStaging);
            if (!removed.ok) {
                const auto rollback = repository_->moveNoReplace(
                    restoreStaging, identity->finalPath);
                return EditorOperationResult::failure(
                    "Could not delete Generated SFX WAV: " + removed.error
                    + (rollback.ok ? std::string{}
                                   : "; rollback failed: " + rollback.error));
            }
        }
        sideEffect = std::make_unique<DeletedGeneratedSfxArtifactSideEffect>(
            repository_, identity->finalPath, restoreStaging,
            preserved.exists, std::move(preserved.bytes));
    }

    return coordinator_.executeWithSideEffect(
        RemoveGeneratedSfxCommand{intent.id}, std::move(sideEffect));
}

} // namespace ArtCade::EditorNative
