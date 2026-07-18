#pragma once

#include "editor-native/app/project_session_id.h"
#include "editor-native/app/sfx_batch.h"
#include "editor-native/app/generated_sfx_output_transaction.h"

#include "artcade/sfx/types.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace ArtCade::EditorNative {

enum class GeneratedSfxJobKind { Preview, Generate };

struct PreviewGeneratedSfxIntent { std::string assetId; };
struct GenerateGeneratedSfxIntent { std::string assetId; };
struct RegenerateAllStaleSfxIntent {};

struct GeneratedSfxJobStamp {
    std::uint64_t token = 0;
    ProjectSessionId projectSessionId = 0;
    std::uint64_t documentRevision = 0;
    std::string assetId;
    std::string recipeFingerprint;
    artcade::sfx::SfxRecipe recipe{};
};

enum class GeneratedSfxCompletionRejection {
    None,
    Superseded,
    ProjectChanged,
    DocumentChanged,
    AssetDeleted,
    RecipeChanged,
};

[[nodiscard]] GeneratedSfxCompletionRejection validateGeneratedSfxCompletion(
    const GeneratedSfxJobStamp& stamp,
    std::uint64_t activeToken,
    ProjectSessionId currentSessionId,
    const ProjectDocument& document);

enum class GeneratedSfxServiceEventKind {
    PreviewReady,
    GenerationReady,
    Failed,
    Discarded,
};

struct GeneratedSfxServiceEvent {
    GeneratedSfxServiceEventKind kind = GeneratedSfxServiceEventKind::Failed;
    GeneratedSfxJobKind jobKind = GeneratedSfxJobKind::Preview;
    GeneratedSfxJobStamp stamp;
    artcade::sfx::FloatAudioBuffer audio;
    GeneratedSfxOutputIdentity output;
    bool regenerating = false;
    bool batchOwned = false;
    std::string message;
};

struct GeneratedSfxServiceRequestResult {
    bool accepted = false;
    std::string message;
};

enum class GeneratedSfxGenerationFailureKind {
    Failed,
    Collision,
};

struct GeneratedSfxGenerationActivity {
    std::string assetId;
    std::string recipeFingerprint;
};

struct GeneratedSfxGenerationDiagnostic {
    GeneratedSfxGenerationFailureKind kind =
        GeneratedSfxGenerationFailureKind::Failed;
    std::string recipeFingerprint;
    std::string message;
};

// Immutable workspace projection input. It is copied on demand; neither UI nor
// status projection can mutate the service's job/diagnostic authority.
struct GeneratedSfxGenerationSnapshot {
    std::optional<GeneratedSfxGenerationActivity> generating;
    std::unordered_map<std::string, GeneratedSfxGenerationDiagnostic> diagnostics;
};

// Workspace-only authority for Generated SFX render jobs and serial batch
// state. It owns immutable request snapshots, cancellation and completion
// validation, but never mutates ProjectDocument and never touches RmlUi/Raylib.
class GeneratedSfxGenerationService {
public:
    explicit GeneratedSfxGenerationService(
        std::shared_ptr<GeneratedSfxOutputRepository> repository = {});
    ~GeneratedSfxGenerationService();

    GeneratedSfxGenerationService(const GeneratedSfxGenerationService&) = delete;
    GeneratedSfxGenerationService& operator=(const GeneratedSfxGenerationService&) = delete;

    [[nodiscard]] GeneratedSfxServiceRequestResult requestPreview(
        const ProjectDocument& document,
        ProjectSessionId sessionId,
        const PreviewGeneratedSfxIntent& intent,
        bool playing);
    [[nodiscard]] GeneratedSfxServiceRequestResult requestGenerate(
        const ProjectDocument& document,
        ProjectSessionId sessionId,
        const std::filesystem::path& projectPath,
        const GenerateGeneratedSfxIntent& intent,
        bool playing);
    [[nodiscard]] GeneratedSfxServiceRequestResult requestRegenerateAllStale(
        const ProjectDocument& document,
        ProjectSessionId sessionId,
        const std::filesystem::path& projectPath,
        RegenerateAllStaleSfxIntent intent,
        bool playing);

    // Poll only on the owner thread. A GenerationReady event reserves the
    // service until resolveGeneration() reports the filesystem/Command result.
    [[nodiscard]] std::optional<GeneratedSfxServiceEvent> poll(
        const ProjectDocument& document,
        ProjectSessionId currentSessionId,
        const std::filesystem::path& currentProjectPath,
        bool playing);
    void resolveGeneration(std::uint64_t token,
                           SfxBatchItemStatus status,
                           std::string message,
                           GeneratedSfxGenerationFailureKind failureKind =
                               GeneratedSfxGenerationFailureKind::Failed);

    void cancelBatch();
    void dismissBatchSummary();
    bool dismissDiagnostic(const std::string& assetId);
    void cancelAll(std::string reason);
    void shutdown();

    bool busy() const;
    [[nodiscard]] GeneratedSfxGenerationSnapshot snapshot() const;
    const SfxBatchState& batchState() const { return batch_; }

private:
    struct JobResult;
    struct ActiveJob;
    struct AwaitingResolution {
        std::uint64_t token = 0;
        std::string assetId;
        std::string recipeFingerprint;
    };

    GeneratedSfxServiceRequestResult start(
        GeneratedSfxJobKind kind,
        const ProjectDocument& document,
        ProjectSessionId sessionId,
        const std::filesystem::path& projectPath,
        const std::string& assetId,
        bool playing,
        bool batchOwned);
    void pumpBatch(const ProjectDocument& document,
                   ProjectSessionId currentSessionId,
                   const std::filesystem::path& currentProjectPath,
                   bool playing);
    void finishBatch();
    void cleanStaging(const std::filesystem::path& path) const;

    std::unique_ptr<ActiveJob> active_;
    std::optional<AwaitingResolution> awaitingResolution_;
    std::uint64_t nextToken_ = 1;
    SfxBatchState batch_;
    std::string cancellationReason_;
    std::unordered_map<std::string, GeneratedSfxGenerationDiagnostic> diagnostics_;
    std::shared_ptr<GeneratedSfxOutputRepository> repository_;
};

} // namespace ArtCade::EditorNative
