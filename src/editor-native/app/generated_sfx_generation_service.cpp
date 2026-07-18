#include "editor-native/app/generated_sfx_generation_service.h"

#include "editor-native/app/generated_sfx_generation_preflight.h"
#include "editor-native/model/generated_sfx_policy.h"
#include "editor-native/model/project_document.h"

#include "artcade/sfx/synthesizer.hpp"
#include "artcade/sfx/recipe_json.hpp"
#include "artcade/sfx/wav_encoder.hpp"

#include <chrono>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

std::string rejectionMessage(GeneratedSfxCompletionRejection rejection) {
    switch (rejection) {
    case GeneratedSfxCompletionRejection::Superseded: return "Superseded";
    case GeneratedSfxCompletionRejection::ProjectChanged: return "Project changed";
    case GeneratedSfxCompletionRejection::DocumentChanged: return "Document changed";
    case GeneratedSfxCompletionRejection::AssetDeleted: return "Deleted";
    case GeneratedSfxCompletionRejection::RecipeChanged: return "Recipe changed";
    case GeneratedSfxCompletionRejection::None: break;
    }
    return {};
}

} // namespace

struct GeneratedSfxGenerationService::JobResult {
    GeneratedSfxJobKind kind = GeneratedSfxJobKind::Preview;
    GeneratedSfxJobStamp stamp;
    artcade::sfx::FloatAudioBuffer audio;
    GeneratedSfxOutputIdentity output;
    bool regenerating = false;
    bool batchOwned = false;
    bool ok = false;
    bool cancelled = false;
    std::string error;
};

struct GeneratedSfxGenerationService::ActiveJob {
    std::uint64_t token = 0;
    ProjectSessionId projectSessionId = 0;
    GeneratedSfxJobKind kind = GeneratedSfxJobKind::Preview;
    std::string assetId;
    std::string recipeFingerprint;
    std::shared_ptr<std::atomic_bool> cancellation;
    std::future<JobResult> future;
};

GeneratedSfxGenerationService::GeneratedSfxGenerationService(
    std::shared_ptr<GeneratedSfxOutputRepository> repository)
    : repository_(repository ? std::move(repository)
                             : std::make_shared<FilesystemGeneratedSfxOutputRepository>()) {}

GeneratedSfxCompletionRejection validateGeneratedSfxCompletion(
    const GeneratedSfxJobStamp& stamp,
    std::uint64_t activeToken,
    ProjectSessionId currentSessionId,
    const ProjectDocument& document) {
    if (stamp.token == 0 || stamp.token != activeToken)
        return GeneratedSfxCompletionRejection::Superseded;
    if (stamp.projectSessionId == 0 || stamp.projectSessionId != currentSessionId)
        return GeneratedSfxCompletionRejection::ProjectChanged;
    const auto* current = document.findGeneratedSfx(stamp.assetId);
    if (!current) return GeneratedSfxCompletionRejection::AssetDeleted;
    if (stamp.recipeFingerprint.empty()
        || artcade::sfx::recipeFingerprint(current->recipe) != stamp.recipeFingerprint
        || !generatedSfxRecipesEqual(current->recipe, stamp.recipe))
        return GeneratedSfxCompletionRejection::RecipeChanged;
    if (stamp.documentRevision != document.revision())
        return GeneratedSfxCompletionRejection::DocumentChanged;
    return GeneratedSfxCompletionRejection::None;
}

GeneratedSfxGenerationService::~GeneratedSfxGenerationService() {
    shutdown();
}

bool GeneratedSfxGenerationService::busy() const {
    return active_ || awaitingResolution_.has_value() || batch_.active;
}

GeneratedSfxGenerationSnapshot GeneratedSfxGenerationService::snapshot() const {
    GeneratedSfxGenerationSnapshot result;
    if (active_ && active_->kind == GeneratedSfxJobKind::Generate) {
        result.generating = GeneratedSfxGenerationActivity{
            active_->assetId, active_->recipeFingerprint};
    } else if (awaitingResolution_) {
        result.generating = GeneratedSfxGenerationActivity{
            awaitingResolution_->assetId,
            awaitingResolution_->recipeFingerprint};
    }
    result.diagnostics = diagnostics_;
    return result;
}

GeneratedSfxServiceRequestResult GeneratedSfxGenerationService::requestPreview(
    const ProjectDocument& document,
    ProjectSessionId sessionId,
    const PreviewGeneratedSfxIntent& intent,
    bool playing) {
    return start(GeneratedSfxJobKind::Preview, document, sessionId, {}, intent.assetId,
                 playing, false);
}

GeneratedSfxServiceRequestResult GeneratedSfxGenerationService::requestGenerate(
    const ProjectDocument& document,
    ProjectSessionId sessionId,
    const std::filesystem::path& projectPath,
    const GenerateGeneratedSfxIntent& intent,
    bool playing) {
    return start(GeneratedSfxJobKind::Generate, document, sessionId, projectPath,
                 intent.assetId, playing, false);
}

GeneratedSfxServiceRequestResult GeneratedSfxGenerationService::start(
    GeneratedSfxJobKind kind,
    const ProjectDocument& document,
    ProjectSessionId sessionId,
    const std::filesystem::path& projectPath,
    const std::string& assetId,
    bool playing,
    bool batchOwned) {
    if (playing) return {false, "Stop Play before previewing or generating SFX"};
    if (sessionId == 0) return {false, "No active project session"};
    if (active_ || awaitingResolution_)
        return {false, "A Generated SFX job is already running"};
    if (batch_.active && !batchOwned)
        return {false, "Finish or cancel the regenerate queue first"};

    const auto* definition = document.findGeneratedSfx(assetId);
    if (!definition) return {false, "Generated SFX no longer exists: " + assetId};

    JobResult request;
    request.kind = kind;
    request.stamp.token = nextToken_++;
    if (nextToken_ == 0) nextToken_ = 1;
    request.stamp.projectSessionId = sessionId;
    request.stamp.documentRevision = document.revision();
    request.stamp.assetId = assetId;
    request.stamp.recipe = definition->recipe;
    request.stamp.recipeFingerprint =
        artcade::sfx::recipeFingerprint(definition->recipe);
    request.batchOwned = batchOwned;

    if (kind == GeneratedSfxJobKind::Generate) {
        const auto preflight = preflightGeneratedSfxGeneration(
            document, *definition,
            projectPath.empty() ? std::filesystem::path{} : projectPath.parent_path(),
            *repository_);
        if (!preflight.allowed()) {
            diagnostics_[assetId] = GeneratedSfxGenerationDiagnostic{
                preflight.blocker == GeneratedSfxGenerationBlocker::Collision
                    ? GeneratedSfxGenerationFailureKind::Collision
                    : GeneratedSfxGenerationFailureKind::Failed,
                request.stamp.recipeFingerprint, preflight.error};
            return {false, preflight.error};
        }
        request.output = preflight.identity;
        request.regenerating = preflight.regenerating();
    }

    auto cancellation = std::make_shared<std::atomic_bool>(false);
    auto future = std::async(std::launch::async,
        [request, cancellation]() mutable {
            if (cancellation->load(std::memory_order_acquire)) {
                request.cancelled = true;
                return request;
            }
            artcade::sfx::SfxSynthesizer synthesizer;
            auto rendered = synthesizer.render(request.stamp.recipe);
            if (!rendered.ok()) {
                request.error = rendered.error().message;
                return request;
            }
            if (cancellation->load(std::memory_order_acquire)) {
                request.cancelled = true;
                return request;
            }
            request.audio = rendered.takeValue();
            if (request.kind == GeneratedSfxJobKind::Generate) {
                artcade::sfx::WavEncoder encoder;
                auto encoded = encoder.encode(request.audio, request.output.stagingPath);
                if (!encoded.ok()) {
                    request.error = encoded.error().message;
                    return request;
                }
                request.audio.samples.clear();
                request.audio.samples.shrink_to_fit();
                if (cancellation->load(std::memory_order_acquire)) {
                    request.cancelled = true;
                    return request;
                }
            }
            request.ok = true;
            return request;
        });

    if (kind == GeneratedSfxJobKind::Generate) diagnostics_.erase(assetId);
    active_ = std::make_unique<ActiveJob>(ActiveJob{
        request.stamp.token, request.stamp.projectSessionId, kind, assetId,
        request.stamp.recipeFingerprint,
        std::move(cancellation), std::move(future)});
    return {true, kind == GeneratedSfxJobKind::Preview
        ? "Rendering SFX preview..."
        : (request.regenerating ? "Regenerating SFX WAV..." : "Generating SFX WAV...")};
}

GeneratedSfxServiceRequestResult
GeneratedSfxGenerationService::requestRegenerateAllStale(
    const ProjectDocument& document,
    ProjectSessionId sessionId,
    const std::filesystem::path& projectPath,
    RegenerateAllStaleSfxIntent,
    bool playing) {
    if (playing) return {false, "Stop Play before regenerating SFX"};
    if (sessionId == 0) return {false, "No active project session"};
    if (busy()) return {false, "A Generated SFX job is already running"};
    if (projectPath.empty())
        return {false, "Save the project before regenerating WAV assets"};

    SfxBatchState next;
    next.active = true;
    next.projectSessionIdAtStart = sessionId;
    for (const auto& definition : document.data().generatedSfx) {
        if (generatedSfxOutputStatus(document, definition)
            != GeneratedSfxOutputStatus::Stale) continue;
        next.items.push_back(SfxBatchItem{definition.id});
    }
    if (next.items.empty()) return {false, "No stale Generated SFX to regenerate"};
    batch_ = std::move(next);
    pumpBatch(document, sessionId, projectPath, playing);
    return {true, "Regenerating " + std::to_string(batch_.items.size())
        + " stale Generated SFX..."};
}

void GeneratedSfxGenerationService::pumpBatch(
    const ProjectDocument& document,
    ProjectSessionId currentSessionId,
    const std::filesystem::path& currentProjectPath,
    bool playing) {
    while (batch_.active && !active_ && !awaitingResolution_) {
        if (batch_.cancelRequested
            || currentSessionId != batch_.projectSessionIdAtStart || playing) {
            const std::string reason = playing ? "Play started"
                : (currentSessionId != batch_.projectSessionIdAtStart
                    ? "Project changed" : "Cancelled");
            for (auto& item : batch_.items) {
                if (item.status == SfxBatchItemStatus::Pending) {
                    item.status = SfxBatchItemStatus::Cancelled;
                    item.message = reason;
                }
            }
        }

        std::optional<std::size_t> next;
        for (std::size_t i = 0; i < batch_.items.size(); ++i) {
            if (batch_.items[i].status == SfxBatchItemStatus::Pending) {
                next = i;
                break;
            }
        }
        if (!next) {
            finishBatch();
            return;
        }

        auto& item = batch_.items[*next];
        batch_.currentIndex = *next;
        const auto* definition = document.findGeneratedSfx(item.id);
        if (!definition) {
            item.status = SfxBatchItemStatus::Skipped;
            item.message = "Deleted";
            continue;
        }
        if (generatedSfxOutputStatus(document, *definition)
            != GeneratedSfxOutputStatus::Stale) {
            item.status = SfxBatchItemStatus::Skipped;
            item.message = "No longer stale";
            continue;
        }
        item.status = SfxBatchItemStatus::Generating;
        item.message.clear();
        const auto started = start(GeneratedSfxJobKind::Generate, document,
                                   currentSessionId, currentProjectPath,
                                   item.id, playing, true);
        if (!started.accepted) {
            item.status = SfxBatchItemStatus::Failed;
            item.message = started.message;
            continue;
        }
        return;
    }
}

std::optional<GeneratedSfxServiceEvent> GeneratedSfxGenerationService::poll(
    const ProjectDocument& document,
    ProjectSessionId currentSessionId,
    const std::filesystem::path& currentProjectPath,
    bool playing) {
    if (active_ && (playing || currentSessionId == 0
                    || currentSessionId != active_->projectSessionId)) {
        // Session validation below remains authoritative; early cancellation
        // merely avoids wasting CPU. A non-batch job carries its own session in
        // the result and is validated when ready.
        cancellationReason_ = playing ? "Play started" : "Project changed";
        active_->cancellation->store(true, std::memory_order_release);
    }

    if (!active_) {
        pumpBatch(document, currentSessionId, currentProjectPath, playing);
        return std::nullopt;
    }
    if (active_->future.wait_for(std::chrono::seconds(0))
        != std::future_status::ready) return std::nullopt;

    const std::uint64_t activeToken = active_->token;
    JobResult completed = active_->future.get();
    active_.reset();
    if (completed.kind == GeneratedSfxJobKind::Generate)
        awaitingResolution_ = AwaitingResolution{
            completed.stamp.token, completed.stamp.assetId,
            completed.stamp.recipeFingerprint};

    GeneratedSfxServiceEvent event;
    event.jobKind = completed.kind;
    event.stamp = completed.stamp;
    event.output = completed.output;
    event.regenerating = completed.regenerating;
    event.batchOwned = completed.batchOwned;

    const auto rejection = validateGeneratedSfxCompletion(
        completed.stamp, activeToken, currentSessionId, document);
    if (completed.cancelled || rejection != GeneratedSfxCompletionRejection::None) {
        cleanStaging(completed.output.stagingPath);
        event.kind = GeneratedSfxServiceEventKind::Discarded;
        event.message = completed.cancelled
            ? (cancellationReason_.empty() ? "Cancelled" : cancellationReason_)
            : rejectionMessage(rejection);
        if (completed.kind == GeneratedSfxJobKind::Generate) {
            resolveGeneration(
                completed.stamp.token,
                completed.cancelled
                        || rejection == GeneratedSfxCompletionRejection::ProjectChanged
                    ? SfxBatchItemStatus::Cancelled : SfxBatchItemStatus::Skipped,
                event.message);
        }
        return event;
    }
    if (!completed.ok) {
        cleanStaging(completed.output.stagingPath);
        event.kind = GeneratedSfxServiceEventKind::Failed;
        event.message = completed.error;
        if (completed.kind == GeneratedSfxJobKind::Generate) {
            diagnostics_[completed.stamp.assetId] =
                GeneratedSfxGenerationDiagnostic{
                    GeneratedSfxGenerationFailureKind::Failed,
                    completed.stamp.recipeFingerprint, event.message};
        }
        if (completed.kind == GeneratedSfxJobKind::Generate) {
            resolveGeneration(completed.stamp.token,
                              SfxBatchItemStatus::Failed, event.message);
        }
        return event;
    }

    if (completed.kind == GeneratedSfxJobKind::Preview) {
        event.kind = GeneratedSfxServiceEventKind::PreviewReady;
        event.audio = std::move(completed.audio);
        return event;
    }

    event.kind = GeneratedSfxServiceEventKind::GenerationReady;
    return event;
}

void GeneratedSfxGenerationService::resolveGeneration(
    std::uint64_t token,
    SfxBatchItemStatus status,
    std::string message,
    GeneratedSfxGenerationFailureKind failureKind) {
    if (!awaitingResolution_ || awaitingResolution_->token != token) return;
    const AwaitingResolution completed = *awaitingResolution_;
    awaitingResolution_.reset();
    if (status == SfxBatchItemStatus::Succeeded) {
        diagnostics_.erase(completed.assetId);
    } else if (status == SfxBatchItemStatus::Failed) {
        diagnostics_[completed.assetId] = GeneratedSfxGenerationDiagnostic{
            failureKind, completed.recipeFingerprint, message};
    }
    for (auto& item : batch_.items) {
        if (item.id == "") continue;
        if (item.status == SfxBatchItemStatus::Generating) {
            item.status = status;
            item.message = std::move(message);
            break;
        }
    }
}

void GeneratedSfxGenerationService::finishBatch() {
    batch_.active = false;
    batch_.cancelRequested = false;
    batch_.summaryVisible = true;
    batch_.succeeded = batch_.failed = batch_.skipped = batch_.cancelled = 0;
    for (const auto& item : batch_.items) {
        switch (item.status) {
        case SfxBatchItemStatus::Succeeded: ++batch_.succeeded; break;
        case SfxBatchItemStatus::Failed: ++batch_.failed; break;
        case SfxBatchItemStatus::Skipped: ++batch_.skipped; break;
        case SfxBatchItemStatus::Cancelled: ++batch_.cancelled; break;
        default: break;
        }
    }
}

void GeneratedSfxGenerationService::cancelBatch() {
    if (!batch_.active) return;
    batch_.cancelRequested = true;
    cancellationReason_ = "Cancelled";
    if (active_) active_->cancellation->store(true, std::memory_order_release);
}

void GeneratedSfxGenerationService::dismissBatchSummary() {
    batch_.summaryVisible = false;
}

bool GeneratedSfxGenerationService::dismissDiagnostic(
    const std::string& assetId) {
    return diagnostics_.erase(assetId) != 0;
}

void GeneratedSfxGenerationService::cancelAll(std::string reason) {
    cancellationReason_ = std::move(reason);
    if (active_) active_->cancellation->store(true, std::memory_order_release);
    if (batch_.active) {
        batch_.cancelRequested = true;
        for (auto& item : batch_.items) {
            if (item.status == SfxBatchItemStatus::Pending) {
                item.status = SfxBatchItemStatus::Cancelled;
                item.message = cancellationReason_;
            }
        }
    }
}

void GeneratedSfxGenerationService::cleanStaging(
    const std::filesystem::path& path) const {
    if (path.empty()) return;
    std::error_code error;
    std::filesystem::remove(path, error);
}

void GeneratedSfxGenerationService::shutdown() {
    cancelAll("Shutdown");
    if (active_) {
        JobResult abandoned = active_->future.get();
        cleanStaging(abandoned.output.stagingPath);
        active_.reset();
    }
    awaitingResolution_.reset();
    resetSfxBatch(batch_);
}

} // namespace ArtCade::EditorNative
