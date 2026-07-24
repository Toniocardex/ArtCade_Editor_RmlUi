#include "sprite_frame_resolve.h"

namespace ArtCade::AppRender {

namespace {

using Modules::SpriteAnimator;

bool has_sprite_frame(const SpriteAnimator::Frame& frame) {
    return frame.w > 0 && frame.h > 0;
}

} // namespace

bool sprite_frame_has_pixels(const SpriteAnimator::Frame& frame) {
    return has_sprite_frame(frame);
}

ResolvedSpriteDraw sprite_frame_resolve(
    const SpriteAnimator* animator,
    EntityId id,
    const SpriteComponent& sprite,
    bool inEditMode)
{
    if (!animator)
        return {};

    const auto current = animator->currentFrame(id);
    if (has_sprite_frame(current))
        return { current, animator->currentClipAssetId(id) };

    if (inEditMode && !sprite.defaultClip.empty())
        return { animator->clipFrame(sprite.defaultClip, 0),
                 animator->clipAssetId(sprite.defaultClip) };

    if (!sprite.spriteAssetId.empty())
        return { animator->firstFrameForAsset(sprite.spriteAssetId),
                 sprite.spriteAssetId };

    return {};
}

} // namespace ArtCade::AppRender
