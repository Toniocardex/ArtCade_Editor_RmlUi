#pragma once

#include "../../../core/types.h"

namespace ArtCade::Modules {
class SpriteAnimator;
}

namespace ArtCade {

/** Register all clips from parsed image assets into the runtime animator. */
void registerAnimationClipsFromAssets(
    Modules::SpriteAnimator& animator,
    const std::vector<ImageAssetDef>& imageAssets);

/** Append v4 animation assets using the composite (asset id, clip id) identity. */
void appendAnimationClipsFromAssets(
    Modules::SpriteAnimator& animator,
    const std::vector<SpriteAnimationAssetDef>& animationAssets);

/**
 * Hot-sync: upsert clip defs from assets and remove stale names without clearClips().
 * Used by Spritesheet Studio so gameplay instances are not left without definitions
 * between clear and re-import in the same frame.
 */
void replaceAnimationClipsFromAssets(
    Modules::SpriteAnimator& animator,
    const std::vector<ImageAssetDef>& imageAssets);

} // namespace ArtCade
