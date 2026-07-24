#pragma once

#include "scene-mutation-result.h"
#include "scene-patch.h"

namespace ArtCade::Modules {

class SceneManager;

/**
 * Applies atomic SceneDef patches and returns declarative invalidation flags.
 * Does not coordinate renderer, lifecycle, or entity registry.
 */
class SceneMutationService {
public:
    explicit SceneMutationService(SceneManager& scenes);

    /**
     * Validates and commits @p patch to the runtime SceneDef for @p sceneId.
     * Invalidation flags reflect fields that actually changed.
     * @return mutation result; revision is unchanged on failure or no-op apply
     */
    SceneMutationResult apply(const SceneId& sceneId, const ScenePatch& patch);

    /** Monotonic revision bumped on every successful scene mutation. */
    uint64_t revision() const { return revision_; }

    /** Used by lifecycle/project load in later phases. */
    uint64_t bump_revision();

    /**
     * Defers revision bumps until commit_batch() so one authoring sync produces
     * a single sceneRevision + merged invalidations.
     */
    void begin_batch();

    /**
     * Ends a batch started with begin_batch().
     * @return merged mutation result; revision bumps once when anything changed
     */
    SceneMutationResult commit_batch();

    bool batch_open() const { return batchOpen_; }

private:
    SceneManager& scenes_;
    uint64_t revision_ = 0;
    bool batchOpen_ = false;
    bool batchChanged_ = false;
    SceneId batchSceneId_;
    SceneInvalidation batchInvalidations_ = SceneInvalidation::None;
};

} // namespace ArtCade::Modules
