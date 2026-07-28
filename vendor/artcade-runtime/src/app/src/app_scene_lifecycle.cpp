#include "../include/app.h"

#include "app_modules.h"

#include "../../modules/scene-system/include/scene-invalidation.h"

namespace ArtCade {

void Application::handleSceneTransition(
    const ArtCade::Modules::SceneTransitionResult& result)
{
    if (!result.changed || !mod_) return;
    pendingSceneInvalidations_ |= result.invalidations;
    if (ArtCade::Modules::scene_invalidation_has(
            result.invalidations,
            ArtCade::Modules::SceneInvalidation::SceneActivation)) {
        // Camera reset is owned by World::onSceneActivated (ADR-0018).
        // ADR-0039 §17: scene transitions (Logic scene.restart/scene.go_to,
        // and Editor Play's first activation) prepare and start immediately -
        // they never go through Application's startup phase machine, which
        // only gates the executable's first splash. This is the sole call
        // site for scene-scoped activation outside loadProject(), so the
        // reset is unconditional: whatever activationState_ was left at by
        // the previous scene (or by nothing, on Editor Play's first frame),
        // resetting to Empty makes prepare() succeed every time.
        if (mod_->gameplaySession) {
            mod_->gameplaySession->resetGameplayActivationState();
            if (mod_->gameplaySession->prepareActiveSceneGameplay()) {
                mod_->gameplaySession->startPreparedActiveSceneGameplay();
            }
        }
    }
}

} // namespace ArtCade
