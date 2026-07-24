#pragma once

#include "../../core/types.h"
#include "../../modules/sprite-animator/include/sprite-animator.h"

#include <string>

namespace ArtCade::AppRender {

struct ResolvedSpriteDraw {
    Modules::SpriteAnimator::Frame frame{};
    std::string assetId;
};

/** @returns true when the frame has non-zero width and height. */
bool sprite_frame_has_pixels(const Modules::SpriteAnimator::Frame& frame);

/**
 * Resolves the drawable sprite frame for an entity (animation clip, default, or static asset).
 * @param animator sprite animator (may be null)
 * @param id entity id for animated instances
 * @param sprite sprite component on the entity
 * @param inEditMode when true, falls back to defaultClip preview frames
 */
ResolvedSpriteDraw sprite_frame_resolve(
    const Modules::SpriteAnimator* animator,
    EntityId id,
    const SpriteComponent& sprite,
    bool inEditMode);

} // namespace ArtCade::AppRender
