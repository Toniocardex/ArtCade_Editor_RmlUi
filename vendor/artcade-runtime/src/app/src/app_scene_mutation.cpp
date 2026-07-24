#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/scene-system/include/scene-invalidation.h"
#include "../../modules/scene-system/include/scene-mutation-result.h"

namespace ArtCade {

void Application::handleSceneMutation(
    const ArtCade::Modules::SceneMutationResult& result)
{
    if (!result.changed || !mod_) return;
    pendingSceneInvalidations_ |= result.invalidations;
}

void Application::queueSceneInvalidations(
    const ArtCade::Modules::SceneInvalidation flags)
{
    if (flags == ArtCade::Modules::SceneInvalidation::None || !mod_) return;

    if (authoringSyncBatchDepth_ > 0) {
        pendingAuthoringInvalidations_ |= flags;
        return;
    }

    ArtCade::Modules::SceneMutationResult result{};
    result.changed = true;
    result.invalidations = flags;
    if (mod_->sceneManager)
        result.sceneId = mod_->sceneManager->activeSceneId();
    if (mod_->gameplaySession)
        result.sceneRevision = mod_->gameplaySession->bumpSceneRevision();
    handleSceneMutation(result);
}

void Application::beginAuthoringSyncBatch() {
    if (!mod_) return;
    ++authoringSyncBatchDepth_;
    if (authoringSyncBatchDepth_ == 1) {
        pendingAuthoringInvalidations_ = ArtCade::Modules::SceneInvalidation::None;
        if (mod_->gameplaySession) mod_->gameplaySession->beginSceneMutationBatch();
    }
}

void Application::endAuthoringSyncBatch() {
    if (!mod_ || authoringSyncBatchDepth_ <= 0) return;
    --authoringSyncBatchDepth_;
    if (authoringSyncBatchDepth_ > 0) return;

    ArtCade::Modules::SceneMutationResult result{};
    if (mod_->gameplaySession)
        result = mod_->gameplaySession->commitSceneMutationBatch();

    result.invalidations |= pendingAuthoringInvalidations_;
    pendingAuthoringInvalidations_ = ArtCade::Modules::SceneInvalidation::None;

    if (result.invalidations != ArtCade::Modules::SceneInvalidation::None
        && !result.changed) {
        result.changed = true;
        if (mod_->sceneManager)
            result.sceneId = mod_->sceneManager->activeSceneId();
        if (mod_->gameplaySession)
            result.sceneRevision = mod_->gameplaySession->bumpSceneRevision();
    }

    if (result.changed) handleSceneMutation(result);
}

} // namespace ArtCade
