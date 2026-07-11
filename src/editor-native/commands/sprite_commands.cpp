#include "editor-native/commands/sprite_commands.h"

#include "editor-native/model/project_document.h"

#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kSpriteInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

const SceneInstanceDef* instanceOf(const ProjectDocument& document,
                                   const SceneId& sceneId, EntityId id) {
    return document.findInstanceInScene(sceneId, id);
}
} // namespace

// ----------------------------------------------------------------------------
// AddSpriteRendererCommand
// ----------------------------------------------------------------------------
AddSpriteRendererCommand::AddSpriteRendererCommand(SceneId sceneId, EntityId id)
    : sceneId_(std::move(sceneId)), id_(id) {}

EditorOperationResult AddSpriteRendererCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = document.findInstanceInScene(sceneId_, id_);
    if (!inst) {
        return EditorOperationResult::failure("No instance with that id in the target scene");
    }
    if (inst->spriteRenderer.has_value()) {
        return EditorOperationResult::failure("Instance already has a sprite renderer");
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot add sprite renderer: layer is locked");
        }
        captured_ = true;
    }
    if (!document.addSpriteRenderer(sceneId_, id_, SpriteRendererComponent{})) {
        return EditorOperationResult::failure("Failed to add sprite renderer");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentAdded(sceneId_, id_, ComponentKind::SpriteRenderer));
}

EditorOperationResult AddSpriteRendererCommand::undo(ProjectDocument& document) {
    if (!document.removeSpriteRenderer(sceneId_, id_)) {
        return EditorOperationResult::failure("Cannot undo sprite renderer add");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentRemoved(sceneId_, id_, ComponentKind::SpriteRenderer));
}

// ----------------------------------------------------------------------------
// RemoveSpriteRendererCommand
// ----------------------------------------------------------------------------
RemoveSpriteRendererCommand::RemoveSpriteRendererCommand(SceneId sceneId, EntityId id)
    : sceneId_(std::move(sceneId)), id_(id) {}

EditorOperationResult RemoveSpriteRendererCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    const SpriteRendererComponent* current = (inst && inst->spriteRenderer)
        ? &*inst->spriteRenderer : nullptr;
    if (!current) {
        return EditorOperationResult::failure("Instance has no sprite renderer");
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot remove sprite renderer: layer is locked");
        }
        removed_  = *current;   // snapshot for an exact undo
        captured_ = true;
    }
    if (!document.removeSpriteRenderer(sceneId_, id_)) {
        return EditorOperationResult::failure("Failed to remove sprite renderer");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentRemoved(sceneId_, id_, ComponentKind::SpriteRenderer));
}

EditorOperationResult RemoveSpriteRendererCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addSpriteRenderer(sceneId_, id_, removed_)) {
        return EditorOperationResult::failure("Cannot undo sprite renderer removal");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentAdded(sceneId_, id_, ComponentKind::SpriteRenderer));
}

// ----------------------------------------------------------------------------
// SetSpriteRendererVisibleCommand
// ----------------------------------------------------------------------------
SetSpriteRendererVisibleCommand::SetSpriteRendererVisibleCommand(SceneId sceneId, EntityId id,
                                                                 bool visible)
    : sceneId_(std::move(sceneId)), id_(id), next_(visible) {}

EditorOperationResult SetSpriteRendererVisibleCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    const SpriteRendererComponent* current = (inst && inst->spriteRenderer)
        ? &*inst->spriteRenderer : nullptr;
    if (!current) {
        return EditorOperationResult::failure("Instance has no sprite renderer");
    }
    if (current->visible == next_) {
        return EditorOperationResult::success(EditorInvalidation::None); // no-op, not undoable
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change sprite visibility: layer is locked");
        }
        previous_ = current->visible;
        captured_ = true;
    }
    if (!document.setSpriteRendererVisible(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set sprite visibility");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteRenderer));
}

EditorOperationResult SetSpriteRendererVisibleCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setSpriteRendererVisible(sceneId_, id_, previous_)) {
        return EditorOperationResult::failure("Cannot undo sprite visibility change");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteRenderer));
}

// ----------------------------------------------------------------------------
// SetSpriteRendererAssetCommand
// ----------------------------------------------------------------------------
SetSpriteRendererAssetCommand::SetSpriteRendererAssetCommand(SceneId sceneId, EntityId id,
                                                             AssetId assetId)
    : sceneId_(std::move(sceneId)), id_(id), next_(std::move(assetId)) {}

EditorOperationResult SetSpriteRendererAssetCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    const SpriteRendererComponent* current = (inst && inst->spriteRenderer)
        ? &*inst->spriteRenderer : nullptr;
    if (!current) {
        return EditorOperationResult::failure("Instance has no sprite renderer");
    }
    // Empty clears the image; a non-empty id must reference an existing image asset.
    if (!next_.empty() && !document.hasImageAsset(next_)) {
        return EditorOperationResult::failure("Unknown image asset: " + next_);
    }
    if (current->imageAssetId == next_ && current->animationAssetId.empty()
        && !inst->spriteAnimator) {
        return EditorOperationResult::success(EditorInvalidation::None); // no-op, not undoable
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change sprite asset: layer is locked");
        }
        previous_ = *current;
        previousAnimator_ = inst->spriteAnimator;
        captured_ = true;
    }
    if (!document.setSpriteRendererAsset(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set sprite asset");
    }
    if (inst->spriteAnimator && !document.removeSpriteAnimator(sceneId_, id_)) {
        return EditorOperationResult::failure("Failed to remove stale SpriteAnimator");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteRenderer));
}

EditorOperationResult SetSpriteRendererAssetCommand::undo(ProjectDocument& document) {
    if (!captured_) {
        return EditorOperationResult::failure("Cannot undo sprite asset change");
    }
    if (previous_.animationAssetId.empty()) {
        if (!document.setSpriteRendererAsset(sceneId_, id_, previous_.imageAssetId)) {
            return EditorOperationResult::failure("Cannot undo sprite asset change");
        }
    } else if (!document.setSpriteRendererAnimation(sceneId_, id_, previous_.animationAssetId)) {
        return EditorOperationResult::failure("Cannot undo sprite asset change");
    }
    if (previousAnimator_) {
        if (!document.addSpriteAnimator(sceneId_, id_, *previousAnimator_)) {
            return EditorOperationResult::failure("Cannot undo SpriteAnimator restore");
        }
    }
    return EditorOperationResult::success(
        kSpriteInvalidation,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteRenderer));
}

SetSpriteRendererAnimationCommand::SetSpriteRendererAnimationCommand(SceneId sceneId, EntityId id,
                                                                     AssetId assetId)
    : sceneId_(std::move(sceneId)), id_(id), next_(std::move(assetId)) {}

EditorOperationResult SetSpriteRendererAnimationCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    const SpriteRendererComponent* current = (inst && inst->spriteRenderer)
        ? &*inst->spriteRenderer : nullptr;
    if (!current) {
        return EditorOperationResult::failure("Instance has no sprite renderer");
    }
    const SpriteAnimationAssetDef* asset = next_.empty()
        ? nullptr : document.findSpriteAnimationAsset(next_);
    if (!next_.empty() && !asset) {
        return EditorOperationResult::failure("Unknown sprite animation asset: " + next_);
    }
    if (asset && asset->clips.empty()) {
        return EditorOperationResult::failure("Sprite animation asset has no clips");
    }
    if (current->animationAssetId == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *inst)) {
            return EditorOperationResult::failure("Cannot change sprite animation: layer is locked");
        }
        previous_ = *current;
        previousAnimator_ = inst->spriteAnimator;
        captured_ = true;
    }
    if (!document.setSpriteRendererAnimation(sceneId_, id_, next_)) {
        return EditorOperationResult::failure("Failed to set sprite animation source");
    }
    if (!next_.empty() && !inst->spriteAnimator) {
        SpriteAnimatorComponent animator;
        if (asset && !asset->clips.empty()) animator.initialClipId = asset->clips.front().id;
        if (!document.addSpriteAnimator(sceneId_, id_, animator)) {
            return EditorOperationResult::failure("Failed to add SpriteAnimator");
        }
    } else if (next_.empty() && inst->spriteAnimator) {
        if (!document.removeSpriteAnimator(sceneId_, id_)) {
            return EditorOperationResult::failure("Failed to remove SpriteAnimator");
        }
    }
    return EditorOperationResult::success(
        kSpriteInvalidation | EditorInvalidation::Inspector,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteRenderer));
}

EditorOperationResult SetSpriteRendererAnimationCommand::undo(ProjectDocument& document) {
    if (!captured_) {
        return EditorOperationResult::failure("Cannot undo sprite animation source change");
    }
    const SceneInstanceDef* inst = instanceOf(document, sceneId_, id_);
    if (inst && inst->spriteAnimator) document.removeSpriteAnimator(sceneId_, id_);
    if (previous_.animationAssetId.empty()) {
        if (!document.setSpriteRendererAsset(sceneId_, id_, previous_.imageAssetId)) {
            return EditorOperationResult::failure("Cannot undo sprite animation source change");
        }
    } else if (!document.setSpriteRendererAnimation(sceneId_, id_, previous_.animationAssetId)) {
        return EditorOperationResult::failure("Cannot undo sprite animation source change");
    }
    if (previousAnimator_ && !document.addSpriteAnimator(sceneId_, id_, *previousAnimator_)) {
        return EditorOperationResult::failure("Cannot undo SpriteAnimator restore");
    }
    return EditorOperationResult::success(
        kSpriteInvalidation | EditorInvalidation::Inspector,
        DomainChange::componentChanged(sceneId_, id_, ComponentKind::SpriteRenderer));
}

} // namespace ArtCade::EditorNative
