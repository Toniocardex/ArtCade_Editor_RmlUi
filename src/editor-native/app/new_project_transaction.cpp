#include "editor-native/app/new_project_transaction.h"

#include "editor-native/app/editor_coordinator.h"

#include <system_error>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

namespace {

NewProjectStage mapSaveStage(ProjectSaveStage stage) {
    switch (stage) {
        case ProjectSaveStage::Validation: return NewProjectStage::Validation;
        case ProjectSaveStage::Serialize: return NewProjectStage::Serialize;
        case ProjectSaveStage::FileWrite: return NewProjectStage::FileWrite;
        case ProjectSaveStage::MarkSaved: return NewProjectStage::Commit;
    }
    return NewProjectStage::FileWrite;
}

struct DestinationSnapshot {
    bool existed = false;
    std::optional<std::string> regularFileBytes;
};

bool cleanupDirectories(
    const std::vector<std::filesystem::path>& createdDirectories,
    std::string& error) {
    std::error_code ec;
    for (const std::filesystem::path& directory : createdDirectories) {
        std::filesystem::remove(directory, ec); // succeeds only while still empty
        if (ec) {
            error = "Could not clean transaction directory: " + ec.message();
            return false;
        }
    }
    return true;
}

bool restoreDestination(const std::filesystem::path& destination,
                        const DestinationSnapshot& snapshot,
                        const std::vector<std::filesystem::path>& createdDirectories,
                        std::string& error) {
    std::error_code ec;
    if (snapshot.regularFileBytes.has_value()) {
        const ProjectTextFileResult restored = writeProjectTextFileAtomically(
            destination, *snapshot.regularFileBytes);
        if (!restored.ok) {
            error = "Could not restore previous destination: " + restored.error.message;
            return false;
        }
    } else if (!snapshot.existed) {
        std::filesystem::remove(destination, ec);
        if (ec) {
            error = "Could not remove uncommitted project file: " + ec.message();
            return false;
        }
    }

    return cleanupDirectories(createdDirectories, error);
}

} // namespace

NewProjectResult NewProjectResult::success(std::filesystem::path path,
                                           EditorOperationResult result) {
    NewProjectResult out;
    out.ok = true;
    out.destination = std::move(path);
    out.operation = std::move(result);
    return out;
}

NewProjectResult NewProjectResult::cancellation() {
    NewProjectResult out;
    out.cancelled = true;
    out.error.stage = NewProjectStage::Cancelled;
    return out;
}

NewProjectResult NewProjectResult::failure(NewProjectStage stage,
                                           std::filesystem::path path,
                                           std::string message) {
    NewProjectResult out;
    out.error = NewProjectError{stage, std::move(path), std::move(message)};
    return out;
}

NewProjectResult createNewProjectTransaction(
    EditorCoordinator& coordinator,
    ProjectDocument candidate,
    std::optional<std::filesystem::path> destination,
    NewProjectTransactionHooks hooks) {
    if (!destination.has_value()) return NewProjectResult::cancellation();
    if (coordinator.isPlaying()) {
        return NewProjectResult::failure(
            NewProjectStage::Commit, *destination,
            "Cannot create a new project while Play is running");
    }

    std::error_code ec;
    const std::filesystem::path absoluteDestination =
        std::filesystem::absolute(*destination, ec).lexically_normal();
    if (ec) {
        return NewProjectResult::failure(
            NewProjectStage::DirectoryCreate, *destination,
            "Could not resolve project destination: " + ec.message());
    }
    const std::filesystem::path parent = absoluteDestination.parent_path();

    DestinationSnapshot snapshot;
    snapshot.existed = std::filesystem::exists(absoluteDestination, ec);
    if (ec) {
        return NewProjectResult::failure(
            NewProjectStage::FileWrite, absoluteDestination,
            "Could not inspect project destination: " + ec.message());
    }
    if (snapshot.existed && std::filesystem::is_regular_file(absoluteDestination, ec)) {
        ProjectTextFileResult previous = readProjectTextFile(absoluteDestination);
        if (!previous.ok) {
            return NewProjectResult::failure(
                NewProjectStage::FileWrite, absoluteDestination,
                "Could not snapshot existing destination: " + previous.error.message);
        }
        snapshot.regularFileBytes = std::move(previous.value);
    } else if (ec) {
        return NewProjectResult::failure(
            NewProjectStage::FileWrite, absoluteDestination,
            "Could not inspect project destination: " + ec.message());
    }

    std::vector<std::filesystem::path> createdDirectories;
    for (std::filesystem::path cursor = parent;
         !cursor.empty() && !std::filesystem::exists(cursor, ec);
         cursor = cursor.parent_path()) {
        if (ec) {
            return NewProjectResult::failure(
                NewProjectStage::DirectoryCreate, cursor,
                "Could not inspect project directory: " + ec.message());
        }
        createdDirectories.push_back(cursor); // deepest first: rollback order
    }
    std::filesystem::create_directories(parent, ec);
    if (ec) {
        const std::string createError = ec.message();
        std::string rollbackError;
        if (!cleanupDirectories(createdDirectories, rollbackError)) {
            return NewProjectResult::failure(
                NewProjectStage::Rollback, parent, std::move(rollbackError));
        }
        return NewProjectResult::failure(
            NewProjectStage::DirectoryCreate, parent,
            "Could not create project directory: " + createError);
    }

    const SaveProjectCandidateFn save = hooks.saveCandidate
        ? std::move(hooks.saveCandidate)
        : SaveProjectCandidateFn{saveProjectDocumentToFile};
    ProjectSaveResult persisted = save(candidate, absoluteDestination);
    if (!persisted.ok) {
        std::string rollbackError;
        if (!restoreDestination(
                absoluteDestination, snapshot, createdDirectories, rollbackError)) {
            return NewProjectResult::failure(
                NewProjectStage::Rollback, absoluteDestination, std::move(rollbackError));
        }
        return NewProjectResult::failure(
            mapSaveStage(persisted.error.stage), persisted.error.path,
            std::move(persisted.error.message));
    }

    const CommitProjectCandidateFn commit = hooks.commitCandidate
        ? std::move(hooks.commitCandidate)
        : CommitProjectCandidateFn{
            [](EditorCoordinator& target, ProjectDocument replacement) {
                return target.replaceProject(std::move(replacement));
            }};
    EditorOperationResult committed = commit(coordinator, std::move(candidate));
    if (!committed.ok) {
        std::string rollbackError;
        if (!restoreDestination(
                absoluteDestination, snapshot, createdDirectories, rollbackError)) {
            return NewProjectResult::failure(
                NewProjectStage::Rollback, absoluteDestination, std::move(rollbackError));
        }
        return NewProjectResult::failure(
            NewProjectStage::Commit, absoluteDestination, committed.error);
    }
    return NewProjectResult::success(absoluteDestination, std::move(committed));
}

} // namespace ArtCade::EditorNative
