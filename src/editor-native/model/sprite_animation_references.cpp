#include "editor-native/model/sprite_animation_references.h"

#include "editor-native/model/project_document.h"

#include "logic-core.h"

#include <variant>

namespace ArtCade::EditorNative {

namespace {

const AssetId* logicAnimationAssetId(const LogicBlockDef& action) {
    const LogicPropertyDef* property = Logic::findProperty(action, "animationAssetId");
    if (!property) return nullptr;
    const auto* ref = std::get_if<LogicAssetReference>(&property->value);
    return ref ? &ref->id : nullptr;
}

const std::string* logicClipId(const LogicBlockDef& action) {
    const LogicPropertyDef* property = Logic::findProperty(action, "clipId");
    if (!property) return nullptr;
    const auto* value = std::get_if<LogicStringValue>(&property->value);
    return value ? &value->value : nullptr;
}

void collectLogicAssetRefs(const ObjectTypeId& objectTypeId, const LogicBoardDef& board,
                           const AssetId& animationAssetId,
                           std::vector<AnimationAssetReference>& out) {
    for (const LogicRuleDef& rule : board.rules) {
        for (std::size_t index = 0; index < rule.actions.size(); ++index) {
            const LogicBlockDef& action = rule.actions[index];
            if (action.typeId != Logic::kAnimationPlayClip) continue;
            const AssetId* assetId = logicAnimationAssetId(action);
            if (!assetId || *assetId != animationAssetId) continue;
            out.push_back(AnimationAssetReference{
                AnimationReferenceKind::LogicPlayClip, objectTypeId, {},
                INVALID_ENTITY, rule.id, index});
        }
    }
}

void collectLogicClipRefs(const ObjectTypeId& objectTypeId, const LogicBoardDef& board,
                          const AssetId& animationAssetId, const std::string& clipId,
                          std::vector<AnimationClipReference>& out) {
    for (const LogicRuleDef& rule : board.rules) {
        for (std::size_t index = 0; index < rule.actions.size(); ++index) {
            const LogicBlockDef& action = rule.actions[index];
            if (action.typeId != Logic::kAnimationPlayClip) continue;
            const AssetId* assetId = logicAnimationAssetId(action);
            const std::string* actionClip = logicClipId(action);
            if (!assetId || *assetId != animationAssetId) continue;
            if (!actionClip || *actionClip != clipId) continue;
            out.push_back(AnimationClipReference{
                AnimationReferenceKind::LogicPlayClip, objectTypeId, {},
                INVALID_ENTITY, rule.id, index});
        }
    }
}

} // namespace

std::vector<AnimationAssetReference> collectAnimationAssetReferences(
    const ProjectDocument& document, const AssetId& animationAssetId) {
    std::vector<AnimationAssetReference> refs;
    if (animationAssetId.empty()) return refs;

    for (const auto& [objectTypeId, type] : document.data().objectTypes) {
        if (type.spriteAnimator
            && type.spriteAnimator->animationAssetId == animationAssetId) {
            refs.push_back(AnimationAssetReference{
                AnimationReferenceKind::ObjectTypeAnimator, objectTypeId});
        }
        if (type.logicBoard) {
            collectLogicAssetRefs(objectTypeId, *type.logicBoard, animationAssetId, refs);
        }
    }

    for (const auto& [sceneId, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (!instance.spriteAnimatorOverride
                || !instance.spriteAnimatorOverride->animationAssetId
                || *instance.spriteAnimatorOverride->animationAssetId != animationAssetId) {
                continue;
            }
            refs.push_back(AnimationAssetReference{
                AnimationReferenceKind::InstanceAnimatorOverride,
                instance.objectTypeId, sceneId, instance.id});
        }
    }
    return refs;
}

std::vector<AnimationClipReference> collectAnimationClipReferences(
    const ProjectDocument& document, const AssetId& animationAssetId,
    const std::string& clipId) {
    std::vector<AnimationClipReference> refs;
    if (animationAssetId.empty() || clipId.empty()) return refs;

    for (const auto& [objectTypeId, type] : document.data().objectTypes) {
        if (type.spriteAnimator
            && type.spriteAnimator->animationAssetId == animationAssetId
            && type.spriteAnimator->defaultClipId == clipId) {
            refs.push_back(AnimationClipReference{
                AnimationReferenceKind::ObjectTypeAnimator, objectTypeId});
        }
        if (type.logicBoard) {
            collectLogicClipRefs(
                objectTypeId, *type.logicBoard, animationAssetId, clipId, refs);
        }
    }

    for (const auto& [sceneId, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (!instance.spriteAnimatorOverride
                || !instance.spriteAnimatorOverride->defaultClipId
                || *instance.spriteAnimatorOverride->defaultClipId != clipId) {
                continue;
            }
            const AssetId* overrideAsset =
                instance.spriteAnimatorOverride->animationAssetId
                ? &*instance.spriteAnimatorOverride->animationAssetId
                : nullptr;
            if (overrideAsset) {
                if (*overrideAsset != animationAssetId) continue;
            } else {
                const EntityDef* type = document.findObjectType(instance.objectTypeId);
                if (!type || !type->spriteAnimator
                    || type->spriteAnimator->animationAssetId != animationAssetId) {
                    continue;
                }
            }
            refs.push_back(AnimationClipReference{
                AnimationReferenceKind::InstanceAnimatorOverride,
                instance.objectTypeId, sceneId, instance.id});
        }
    }
    return refs;
}

std::vector<AnimationSourceReference> collectAnimationSourceReferences(
    const ProjectDocument& document, const AssetId& imageAssetId) {
    std::vector<AnimationSourceReference> refs;
    if (imageAssetId.empty()) return refs;
    for (const SpriteAnimationAssetDef& asset : document.data().spriteAnimationAssets) {
        if (asset.sourceImageAssetId == imageAssetId) {
            refs.push_back(AnimationSourceReference{asset.id});
        }
    }
    return refs;
}

} // namespace ArtCade::EditorNative
