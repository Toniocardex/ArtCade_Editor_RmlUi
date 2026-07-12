#pragma once

#include "editor-native/app/project_file.h"
#include "editor-native/model/project_document.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace ArtCade::EditorNative {

class EditorCoordinator;

enum class NewProjectStage {
    Cancelled,
    DirectoryCreate,
    Validation,
    Serialize,
    FileWrite,
    Commit,
    Rollback,
};

struct NewProjectError {
    NewProjectStage       stage = NewProjectStage::Cancelled;
    std::filesystem::path path;
    std::string           message;
};

struct NewProjectResult {
    bool                  ok = false;
    bool                  cancelled = false;
    std::filesystem::path destination;
    EditorOperationResult operation;
    NewProjectError       error;

    static NewProjectResult success(std::filesystem::path path,
                                    EditorOperationResult result);
    static NewProjectResult cancellation();
    static NewProjectResult failure(NewProjectStage stage,
                                    std::filesystem::path path,
                                    std::string message);
};

using SaveProjectCandidateFn = std::function<ProjectSaveResult(
    const ProjectDocument&, const std::filesystem::path&)>;
using CommitProjectCandidateFn = std::function<EditorOperationResult(
    EditorCoordinator&, ProjectDocument)>;

// Explicit dependencies keep the production transaction deterministic while
// allowing core tests to force serialization/write/commit failures that are
// otherwise platform-dependent.
struct NewProjectTransactionHooks {
    SaveProjectCandidateFn   saveCandidate;
    CommitProjectCandidateFn commitCandidate;
};

// Transactional New Project boundary. A missing destination is cancellation.
// No coordinator state changes before the candidate is durably written; an
// unexpected commit failure restores/removes the destination and any directories
// created solely by this transaction.
NewProjectResult createNewProjectTransaction(
    EditorCoordinator& coordinator,
    ProjectDocument candidate,
    std::optional<std::filesystem::path> destination,
    NewProjectTransactionHooks hooks = {});

} // namespace ArtCade::EditorNative
