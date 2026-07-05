#include "editor-native/model/sprite_render_view.h"

#include "editor-native/model/project_document.h"

namespace ArtCade::EditorNative {

namespace {
// The clip an entity shows: the animator's initial clip, else the asset default,
// else the first clip. Its imageId is the sheet to render and its first frame the
// still shown in the editor (the runtime playhead takes over during Play).
const SpriteAnimationClipDef* activeClip(const SpriteAnimationAssetDef& asset,
                                         const SpriteAnimatorComponent* animator) {
    std::string clipId = animator ? animator->initialClipId : std::string();
    if (clipId.empty()) clipId = asset.defaultClipId;
    if (!clipId.empty()) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == clipId) return &clip;
        }
    }
    return asset.clips.empty() ? nullptr : &asset.clips.front();
}
} // namespace

SpriteRenderView resolveSpriteRenderer(const ProjectDocument& document,
                                       const SceneId& sceneId, EntityId entityId) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId, entityId);
    if (!instance) return SpriteRenderView{};

    // 1. An instance override always wins.
    if (instance->spriteRenderer.has_value()) {
        SpriteRenderView view = spriteRenderViewOf(*instance);
        if (!view.animationAssetId.empty()) {
            const SpriteAnimationAssetDef* asset =
                document.findSpriteAnimationAsset(view.animationAssetId);
            if (!asset) return SpriteRenderView{};
            const SpriteAnimationClipDef* clip = activeClip(*asset,
                instance->spriteAnimator ? &*instance->spriteAnimator : nullptr);
            if (!clip) return SpriteRenderView{};   // no clips: nothing to show
            view.assetId = clip->imageId;           // the active clip's own sheet
            if (!clip->frames.empty()) {
                const SpriteAnimationFrameDef& frame = clip->frames.front();
                view.sourceRect = AnimationFrameRect{
                    static_cast<float>(frame.x), static_cast<float>(frame.y),
                    static_cast<float>(frame.width), static_cast<float>(frame.height)};
                view.hasSourceRect = frame.width > 0 && frame.height > 0;
            } else {
                view.sourceRect = AnimationFrameRect{};
                view.hasSourceRect = false;
            }
        }
        return view;
    }

    // 2. Otherwise inherit from the object type when it carries a sprite image.
    const auto& types = document.data().objectTypes;
    const auto it = types.find(instance->objectTypeId);
    if (it != types.end() && !it->second.sprite.spriteAssetId.empty()) {
        return SpriteRenderView{true,
                                it->second.visible,
                                it->second.sprite.spriteAssetId,
                                {},
                                {},
                                false,
                                ComponentOrigin::EntityDefinition};
    }

    // 3. Neither: the entity has no sprite renderer.
    return SpriteRenderView{};
}

} // namespace ArtCade::EditorNative
