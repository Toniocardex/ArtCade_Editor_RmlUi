#include "editor-native/commands/sprite_animation_commands.h"

#include "editor-native/model/project_document.h"

#include <cmath>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kAnimatorInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

const SceneInstanceDef* instanceOf(const ProjectDocument& document,
                                   const SceneId& sceneId, EntityId id) {
    return document.findInstanceInScene(sceneId, id);
}

bool clipBelongsToAsset(const SpriteAnimationAssetDef& asset, const std::string& clipId) {
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (clip.id == clipId) return true;
    }
    return false;
}
} // namespace

AddSpriteAnimatorCommand::AddSpriteAnimatorCommand(
    SceneId sceneId, EntityId id, SpriteAnimatorComponent component)
    : sceneId_(std::move(sceneId)), id_(id), component_(std::move(component)) {}

EditorOperationResult AddSpriteAnimatorCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    if (!inst || inst->spriteAnimator) {
        return EditorOperationResult::failure("Cannot add SpriteAnimator");
    }
    if (!inst->spriteRenderer || inst->spriteRenderer->animationAssetId.empty()) {
        return EditorOperationResult::failure("SpriteAnimator requires an animation source");
    }
    if (!std::isfinite(component_.playbackSpeed) || component_.playbackSpeed <= 0.f) {
        return EditorOperationResult::failure("SpriteAnimator playback speed must be positive");
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot add SpriteAnimator: layer is locked");
        }
        captured_ = true;
    }
    if (!document.addSpriteAnimator(sceneId_, id_, component_)) {
        return EditorOperationResult::failure("Failed to add SpriteAnimator");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentAdded(sceneId_, id_, ComponentKind::SpriteAnimator));
}

EditorOperationResult AddSpriteAnimatorCommand::undo(ProjectDocument& document) {
    if (!document.removeSpriteAnimator(sceneId_, id_)) {
        return EditorOperationResult::failure("Cannot undo SpriteAnimator add");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentRemoved(sceneId_, id_, ComponentKind::SpriteAnimator));
}

RemoveSpriteAnimatorCommand::RemoveSpriteAnimatorCommand(SceneId sceneId, EntityId id)
    : sceneId_(std::move(sceneId)), id_(id) {}

EditorOperationResult RemoveSpriteAnimatorCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    if (!inst || !inst->spriteAnimator) {
        return EditorOperationResult::failure("Instance has no SpriteAnimator");
    }
    if (inst->spriteRenderer && !inst->spriteRenderer->animationAssetId.empty()) {
        return EditorOperationResult::failure("Animation source requires SpriteAnimator");
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot remove SpriteAnimator: layer is locked");
        }
        removed_ = *inst->spriteAnimator;
        captured_ = true;
    }
    if (!document.removeSpriteAnimator(sceneId_, id_)) {
        return EditorOperationResult::failure("Failed to remove SpriteAnimator");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentRemoved(sceneId_, id_, ComponentKind::SpriteAnimator));
}

EditorOperationResult RemoveSpriteAnimatorCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addSpriteAnimator(sceneId_, id_, removed_)) {
        return EditorOperationResult::failure("Cannot undo SpriteAnimator removal");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentAdded(sceneId_, id_, ComponentKind::SpriteAnimator));
}

SetSpriteAnimatorInitialClipCommand::SetSpriteAnimatorInitialClipCommand(
    SceneId sceneId, EntityId id, std::string clipId)
    : sceneId_(std::move(sceneId)), id_(id), next_(std::move(clipId)) {}

EditorOperationResult SetSpriteAnimatorInitialClipCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    if (!inst || !inst->spriteAnimator || !inst->spriteRenderer) {
        return EditorOperationResult::failure("SpriteAnimator is missing");
    }
    const SpriteAnimationAssetDef* asset =
        document.findSpriteAnimationAsset(inst->spriteRenderer->animationAssetId);
    if (!asset || !clipBelongsToAsset(*asset, next_)) {
        return EditorOperationResult::failure("Initial clip must belong to the animation asset");
    }
    if (inst->spriteAnimator->initialClipId == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change initial clip: layer is locked");
        }
        previous_ = inst->spriteAnimator->initialClipId;
        captured_ = true;
    }
    if (!document.setSpriteAnimatorInitialClip(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set initial clip");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteAnimator));
}

EditorOperationResult SetSpriteAnimatorInitialClipCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setSpriteAnimatorInitialClip(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo initial clip change");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteAnimator));
}

SetSpriteAnimatorPlaybackSpeedCommand::SetSpriteAnimatorPlaybackSpeedCommand(
    SceneId sceneId, EntityId id, float speed)
    : sceneId_(std::move(sceneId)), id_(id), next_(speed) {}

EditorOperationResult SetSpriteAnimatorPlaybackSpeedCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(next_) || next_ <= 0.f) {
        return EditorOperationResult::failure("Playback speed must be positive");
    }
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    if (!inst || !inst->spriteAnimator) {
        return EditorOperationResult::failure("SpriteAnimator is missing");
    }
    if (inst->spriteAnimator->playbackSpeed == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change playback speed: layer is locked");
        }
        previous_ = inst->spriteAnimator->playbackSpeed;
        captured_ = true;
    }
    if (!document.setSpriteAnimatorPlaybackSpeed(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set playback speed");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteAnimator));
}

EditorOperationResult SetSpriteAnimatorPlaybackSpeedCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setSpriteAnimatorPlaybackSpeed(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo playback speed change");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteAnimator));
}

SetSpriteAnimatorAutoPlayCommand::SetSpriteAnimatorAutoPlayCommand(
    SceneId sceneId, EntityId id, bool autoPlay)
    : sceneId_(std::move(sceneId)), id_(id), next_(autoPlay) {}

EditorOperationResult SetSpriteAnimatorAutoPlayCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    if (!inst || !inst->spriteAnimator) {
        return EditorOperationResult::failure("SpriteAnimator is missing");
    }
    if (inst->spriteAnimator->autoPlay == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change autoplay: layer is locked");
        }
        previous_ = inst->spriteAnimator->autoPlay;
        captured_ = true;
    }
    if (!document.setSpriteAnimatorAutoPlay(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set autoplay");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteAnimator));
}

EditorOperationResult SetSpriteAnimatorAutoPlayCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setSpriteAnimatorAutoPlay(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo autoplay change");
    }
    return EditorOperationResult::success(
        kAnimatorInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteAnimator));
}

} // namespace ArtCade::EditorNative
