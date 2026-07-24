#pragma once

#include "../../../core/types.h"
#include "scene-invalidation.h"

namespace ArtCade::Modules {

enum class SceneMutationError {
    None,
    SceneNotFound,
    InvalidPatch,
};

/**
 * Outcome of SceneMutationService::apply().
 * @p sceneRevision is the global monotonic runtime revision after a successful commit.
 */
struct SceneMutationResult {
    bool changed = false;
    SceneMutationError error = SceneMutationError::None;

    SceneId sceneId;
    uint64_t sceneRevision = 0;
    SceneInvalidation invalidations = SceneInvalidation::None;
};

} // namespace ArtCade::Modules
