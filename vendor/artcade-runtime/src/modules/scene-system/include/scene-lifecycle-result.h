#pragma once

#include "../../../core/types.h"
#include "scene-invalidation.h"

namespace ArtCade::Modules {

enum class SceneTransitionError {
    None,
    SceneNotFound,
    RestoreFailed,
};

/**
 * Outcome of SceneLifecycleService load / restart / fade transition commit.
 * @p sceneRevision is the global monotonic revision after a successful commit.
 */
struct SceneTransitionResult {
    bool changed = false;
    SceneTransitionError error = SceneTransitionError::None;

    SceneId sceneId;
    uint64_t sceneRevision = 0;
    SceneInvalidation invalidations = SceneInvalidation::None;
};

} // namespace ArtCade::Modules
