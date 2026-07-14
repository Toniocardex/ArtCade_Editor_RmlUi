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

ResolvedSpritePresentation resolveSpritePresentation(
    const EntityDef& objectType, const SceneInstanceDef& instance) {
    ResolvedSpritePresentation resolved;

    if (objectType.spriteRenderer) {
        const SpriteRendererOverride* delta = instance.spriteRendererOverride
            ? &*instance.spriteRendererOverride : nullptr;
        if (!delta || !delta->capabilityEnabled || *delta->capabilityEnabled) {
            resolved.renderer = *objectType.spriteRenderer;
            if (delta) {
                if (delta->imageAssetId) {
                    resolved.renderer->imageAssetId = *delta->imageAssetId;
                }
                if (delta->animationAssetId) {
                    resolved.renderer->animationAssetId = *delta->animationAssetId;
                }
                if (delta->visible) resolved.renderer->visible = *delta->visible;
                resolved.rendererOrigin = (delta->imageAssetId || delta->animationAssetId
                                             || delta->visible || delta->capabilityEnabled)
                    ? ComponentOrigin::InstanceOverride
                    : ComponentOrigin::EntityDefinition;
            } else {
                resolved.rendererOrigin = ComponentOrigin::EntityDefinition;
            }
        }
    }

    if (objectType.spriteAnimator) {
        const SpriteAnimatorOverride* delta = instance.spriteAnimatorOverride
            ? &*instance.spriteAnimatorOverride : nullptr;
        if (!delta || !delta->capabilityEnabled || *delta->capabilityEnabled) {
            resolved.animator = *objectType.spriteAnimator;
            if (delta) {
                if (delta->initialClipId) {
                    resolved.animator->initialClipId = *delta->initialClipId;
                }
                if (delta->autoPlay) resolved.animator->autoPlay = *delta->autoPlay;
                if (delta->playbackSpeed) {
                    resolved.animator->playbackSpeed = *delta->playbackSpeed;
                }
                resolved.animatorOrigin = (delta->initialClipId || delta->autoPlay
                                             || delta->playbackSpeed
                                             || delta->capabilityEnabled)
                    ? ComponentOrigin::InstanceOverride
                    : ComponentOrigin::EntityDefinition;
            } else {
                resolved.animatorOrigin = ComponentOrigin::EntityDefinition;
            }
        }
    }

    // Animator without a renderer is never materializable presentation.
    if (!resolved.renderer) {
        resolved.animator.reset();
        resolved.animatorOrigin = ComponentOrigin::None;
    }
    return resolved;
}

SpriteRenderView resolveSpriteRenderer(const ProjectDocument& document,
                                       const SceneId& sceneId, EntityId entityId) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId, entityId);
    if (!instance) return SpriteRenderView{};
    const EntityDef* objectType = document.findObjectType(instance->objectTypeId);
    ResolvedSpritePresentation presentation;
    if (objectType) {
        presentation = resolveSpritePresentation(*objectType, *instance);
    }
    if (!presentation.renderer) return SpriteRenderView{};

    SpriteRenderView view{
        true,
        presentation.renderer->visible,
        presentation.renderer->imageAssetId,
        presentation.renderer->animationAssetId,
        {},
        false,
        presentation.rendererOrigin,
    };
    if (!view.animationAssetId.empty()) {
        const SpriteAnimationAssetDef* asset =
            document.findSpriteAnimationAsset(view.animationAssetId);
        if (!asset) return SpriteRenderView{};
        const SpriteAnimationClipDef* clip = activeClip(
            *asset, presentation.animator ? &*presentation.animator : nullptr);
        if (!clip) return SpriteRenderView{};
        view.assetId = clip->imageId;
        if (!clip->frames.empty()) {
            const SpriteAnimationFrameDef& frame = clip->frames.front();
            view.sourceRect = AnimationFrameRect{
                static_cast<float>(frame.x), static_cast<float>(frame.y),
                static_cast<float>(frame.width), static_cast<float>(frame.height)};
            view.hasSourceRect = frame.width > 0 && frame.height > 0;
        }
    }
    return view;
}

} // namespace ArtCade::EditorNative
