#pragma once

#include "core/types.h"

#include <cstddef>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

class ProjectDocument;

// Canonical hard-reference scans for animation integrity. Delete guards,
// validator dialogs, Inspector, and Problems must all consume these — never
// re-scan the document ad hoc inside individual Commands.

enum class AnimationReferenceKind {
    ObjectTypeAnimator,
    InstanceAnimatorOverride,
    LogicPlayClip,
};

struct AnimationAssetReference {
    AnimationReferenceKind kind = AnimationReferenceKind::ObjectTypeAnimator;
    ObjectTypeId objectTypeId;
    SceneId sceneId;
    EntityId entityId = INVALID_ENTITY;
    std::string ruleId;
    std::size_t actionIndex = 0;
};

struct AnimationClipReference {
    AnimationReferenceKind kind = AnimationReferenceKind::ObjectTypeAnimator;
    ObjectTypeId objectTypeId;
    SceneId sceneId;
    EntityId entityId = INVALID_ENTITY;
    std::string ruleId;
    std::size_t actionIndex = 0;
};

struct AnimationSourceReference {
    AssetId animationAssetId;
};

std::vector<AnimationAssetReference> collectAnimationAssetReferences(
    const ProjectDocument& document, const AssetId& animationAssetId);

std::vector<AnimationClipReference> collectAnimationClipReferences(
    const ProjectDocument& document, const AssetId& animationAssetId,
    const std::string& clipId);

std::vector<AnimationSourceReference> collectAnimationSourceReferences(
    const ProjectDocument& document, const AssetId& imageAssetId);

} // namespace ArtCade::EditorNative
