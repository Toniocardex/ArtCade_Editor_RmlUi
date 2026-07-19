#include "editor-native/model/sprite_render_view.h"

#include "editor-native/model/project_document.h"

namespace ArtCade::EditorNative {

namespace {

const SpriteAnimationClipDef* activeClip(const SpriteAnimationAssetDef& asset,
                                         const SpriteAnimatorComponent* animator) {
    const std::string clipId = animator ? animator->defaultClipId : std::string();
    if (!clipId.empty()) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            if (clip.id == clipId) return &clip;
        }
        // Non-empty defaultClipId that does not resolve is invalid — never
        // silently fall through to clips.front().
        return nullptr;
    }
    return asset.clips.empty() ? nullptr : &asset.clips.front();
}

const SpriteFrameDef* findFrame(const SpriteAnimationAssetDef& asset,
                                const SpriteFrameId& frameId) {
    for (const SpriteFrameDef& frame : asset.frames) {
        if (frame.id == frameId) return &frame;
    }
    return nullptr;
}

bool assetOwnsClip(const SpriteAnimationAssetDef& asset, const std::string& clipId) {
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (clip.id == clipId) return true;
    }
    return false;
}

} // namespace

ResolvedSpriteOwnership resolveSpriteOwnership(
    const EntityDef& objectType, const SceneInstanceDef& instance) {
    ResolvedSpriteOwnership resolved;

    if (objectType.spriteRenderer) {
        const SpriteRendererOverride* delta = instance.spriteRendererOverride
            ? &*instance.spriteRendererOverride : nullptr;
        if (!delta || !delta->capabilityEnabled || *delta->capabilityEnabled) {
            resolved.renderer = *objectType.spriteRenderer;
            if (delta) {
                if (delta->imageAssetId) {
                    resolved.renderer->imageAssetId = *delta->imageAssetId;
                }
                if (delta->visible) resolved.renderer->visible = *delta->visible;
                resolved.rendererOrigin = (delta->imageAssetId || delta->visible
                                             || delta->capabilityEnabled)
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
                if (delta->animationAssetId) {
                    resolved.animator->animationAssetId = *delta->animationAssetId;
                }
                if (delta->defaultClipId) {
                    resolved.animator->defaultClipId = *delta->defaultClipId;
                }
                if (delta->autoPlay) resolved.animator->autoPlay = *delta->autoPlay;
                if (delta->playbackSpeed) {
                    resolved.animator->playbackSpeed = *delta->playbackSpeed;
                }
                resolved.animatorOrigin = (delta->animationAssetId || delta->defaultClipId
                                             || delta->autoPlay || delta->playbackSpeed
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

ResolvedSpriteAnimator resolveSpriteAnimator(
    const EntityDef& objectType, const SceneInstanceDef& instance) {
    const ResolvedSpriteOwnership ownership = resolveSpriteOwnership(objectType, instance);
    ResolvedSpriteAnimator resolved;
    resolved.animator = ownership.animator;
    resolved.origin = ownership.animatorOrigin;
    if (instance.spriteAnimatorOverride) {
        const SpriteAnimatorOverride& delta = *instance.spriteAnimatorOverride;
        resolved.explicitAnimationAsset = delta.animationAssetId.has_value();
        resolved.explicitDefaultClip = delta.defaultClipId.has_value();
        resolved.explicitAutoPlay = delta.autoPlay.has_value();
        resolved.explicitPlaybackSpeed = delta.playbackSpeed.has_value();
    }
    return resolved;
}

ResolvedSpriteDraw resolveSpriteDraw(
    const ProjectDocument& document,
    const std::optional<SpriteRendererComponent>& renderer,
    const std::optional<SpriteAnimatorComponent>& animator,
    const AnimationFrameRect* runtimeFrame) {
    ResolvedSpriteDraw draw;
    if (!renderer) return draw;

    draw.present = true;
    draw.visible = renderer->visible;
    draw.imageAssetId = renderer->imageAssetId;

    if (runtimeFrame) {
        draw.sourceRect = *runtimeFrame;
        draw.hasSourceRect = runtimeFrame->w > 0.f && runtimeFrame->h > 0.f;
        if (animator && !animator->animationAssetId.empty()) {
            const SpriteAnimationAssetDef* asset =
                document.findSpriteAnimationAsset(animator->animationAssetId);
            if (asset) draw.imageAssetId = asset->sourceImageAssetId;
        }
        return draw;
    }

    if (!animator || animator->animationAssetId.empty()) return draw;

    const SpriteAnimationAssetDef* asset =
        document.findSpriteAnimationAsset(animator->animationAssetId);
    if (!asset) {
        draw.animatorInvalid = true;
        draw.diagnosticCode = "ANIMATION_MISSING_ASSET";
        draw.diagnosticMessage =
            "SpriteAnimator references missing animation asset " + animator->animationAssetId;
        // Edit fallback: keep static renderer image when present.
        return draw;
    }

    const SpriteAnimationClipDef* clip = activeClip(*asset, &*animator);
    if (!clip) {
        draw.animatorInvalid = true;
        draw.diagnosticCode = "ANIMATION_MISSING_CLIP";
        draw.diagnosticMessage =
            "SpriteAnimator defaultClipId is missing from animation asset "
            + animator->animationAssetId;
        return draw;
    }

    draw.imageAssetId = asset->sourceImageAssetId;
    if (!clip->frameIds.empty()) {
        const SpriteFrameDef* frame = findFrame(*asset, clip->frameIds.front());
        if (!frame) {
            draw.animatorInvalid = true;
            draw.diagnosticCode = "ANIMATION_MISSING_FRAME";
            draw.diagnosticMessage =
                "Animation clip frameId is missing from the asset frame pool";
            draw.imageAssetId = renderer->imageAssetId;
            return draw;
        }
        draw.sourceRect = AnimationFrameRect{
            static_cast<float>(frame->x), static_cast<float>(frame->y),
            static_cast<float>(frame->width), static_cast<float>(frame->height)};
        draw.hasSourceRect = frame->width > 0 && frame->height > 0;
    }
    return draw;
}

SpriteRenderView resolveSpriteRenderer(const ProjectDocument& document,
                                       const SceneId& sceneId, EntityId entityId) {
    const SceneInstanceDef* instance = document.findInstanceInScene(sceneId, entityId);
    if (!instance) return SpriteRenderView{};
    const EntityDef* objectType = document.findObjectType(instance->objectTypeId);
    ResolvedSpriteOwnership ownership;
    if (objectType) ownership = resolveSpriteOwnership(*objectType, *instance);
    if (!ownership.renderer) return SpriteRenderView{};

    const ResolvedSpriteDraw draw = resolveSpriteDraw(
        document, ownership.renderer, ownership.animator, nullptr);

    SpriteRenderView view;
    view.present = draw.present;
    view.visible = draw.visible;
    view.assetId = draw.imageAssetId;
    view.animationAssetId = ownership.animator
        ? ownership.animator->animationAssetId : AssetId{};
    view.sourceRect = draw.sourceRect;
    view.hasSourceRect = draw.hasSourceRect;
    view.origin = ownership.rendererOrigin;
    view.animatorInvalid = draw.animatorInvalid;
    view.diagnosticCode = draw.diagnosticCode;
    view.diagnosticMessage = draw.diagnosticMessage;
    return view;
}

std::vector<AnimationAuthoringDiagnostic> collectAnimationAuthoringDiagnostics(
    const ProjectDocument& document) {
    std::vector<AnimationAuthoringDiagnostic> out;

    for (const auto& [objectTypeId, type] : document.data().objectTypes) {
        if (!type.spriteAnimator) continue;
        const SpriteAnimatorComponent& animator = *type.spriteAnimator;
        if (animator.animationAssetId.empty()) {
            out.push_back(AnimationAuthoringDiagnostic{
                "ANIMATION_EMPTY_ASSET",
                "Object Type SpriteAnimator has no animation asset",
                objectTypeId, {}, {}, INVALID_ENTITY});
            continue;
        }
        const SpriteAnimationAssetDef* asset =
            document.findSpriteAnimationAsset(animator.animationAssetId);
        if (!asset) {
            out.push_back(AnimationAuthoringDiagnostic{
                "ANIMATION_MISSING_ASSET",
                "Object Type SpriteAnimator references missing animation asset "
                    + animator.animationAssetId,
                objectTypeId, animator.animationAssetId, {}, INVALID_ENTITY});
            continue;
        }
        if (!animator.defaultClipId.empty()
            && !assetOwnsClip(*asset, animator.defaultClipId)) {
            out.push_back(AnimationAuthoringDiagnostic{
                "ANIMATION_MISSING_CLIP",
                "Object Type SpriteAnimator defaultClipId is missing from "
                    + animator.animationAssetId,
                objectTypeId, animator.animationAssetId, {}, INVALID_ENTITY});
        }
        if (!asset->sourceImageAssetId.empty()
            && !document.hasImageAsset(asset->sourceImageAssetId)) {
            out.push_back(AnimationAuthoringDiagnostic{
                "ANIMATION_MISSING_SOURCE",
                "Animation asset " + asset->id + " references missing source image "
                    + asset->sourceImageAssetId,
                objectTypeId, asset->id, {}, INVALID_ENTITY});
        }
    }

    for (const auto& [sceneId, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            const EntityDef* type = document.findObjectType(instance.objectTypeId);
            if (!type) continue;
            const ResolvedSpriteOwnership ownership =
                resolveSpriteOwnership(*type, instance);
            const ResolvedSpriteDraw draw = resolveSpriteDraw(
                document, ownership.renderer, ownership.animator, nullptr);
            if (!draw.animatorInvalid) continue;
            out.push_back(AnimationAuthoringDiagnostic{
                draw.diagnosticCode.empty() ? "ANIMATION_INVALID" : draw.diagnosticCode,
                draw.diagnosticMessage,
                instance.objectTypeId,
                ownership.animator ? ownership.animator->animationAssetId : AssetId{},
                sceneId,
                instance.id});
        }
    }

    for (const SpriteAnimationAssetDef& asset : document.data().spriteAnimationAssets) {
        for (const SpriteAnimationClipDef& clip : asset.clips) {
            for (const SpriteFrameId& frameId : clip.frameIds) {
                if (!findFrame(asset, frameId)) {
                    out.push_back(AnimationAuthoringDiagnostic{
                        "ANIMATION_MISSING_FRAME",
                        "Clip " + clip.id + " on " + asset.id
                            + " references missing frame " + frameId,
                        {}, asset.id, {}, INVALID_ENTITY});
                }
            }
        }
    }

    return out;
}

} // namespace ArtCade::EditorNative
