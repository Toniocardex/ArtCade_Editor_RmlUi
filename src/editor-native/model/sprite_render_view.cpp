#include "editor-native/model/sprite_render_view.h"

#include "editor-native/model/project_document.h"

namespace ArtCade::EditorNative {

namespace {
AnimationFrameRect firstFrameRect(const SpriteAnimationAssetDef& asset,
                                  const SpriteAnimatorComponent* animator) {
    const std::string& clipId = animator ? animator->initialClipId : std::string();
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (!clipId.empty() && clip.id != clipId) continue;
        if (!clip.frames.empty()) {
            const SpriteAnimationFrameDef& frame = clip.frames.front();
            return AnimationFrameRect{
                static_cast<float>(frame.x),
                static_cast<float>(frame.y),
                static_cast<float>(frame.width),
                static_cast<float>(frame.height),
            };
        }
        if (!clipId.empty()) break;
    }
    return {};
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
            view.assetId = asset->imageId;
            view.sourceRect = firstFrameRect(*asset,
                instance->spriteAnimator ? &*instance->spriteAnimator : nullptr);
            view.hasSourceRect = view.sourceRect.w > 0.f && view.sourceRect.h > 0.f;
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
