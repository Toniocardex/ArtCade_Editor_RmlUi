#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/scene-system/include/scene-invalidation.h"

namespace ArtCade {

void Application::applyPendingSceneInvalidations() {
    if (!mod_) return;

    const ArtCade::Modules::SceneInvalidation flags = pendingSceneInvalidations_;
    pendingSceneInvalidations_ = ArtCade::Modules::SceneInvalidation::None;
    if (flags == ArtCade::Modules::SceneInvalidation::None) return;

    if (ArtCade::Modules::scene_invalidation_needs_collision_rebuild(flags)
        && mod_->gameplaySession) {
        mod_->gameplaySession->rebuildWorldCollision();
    }
}

} // namespace ArtCade
