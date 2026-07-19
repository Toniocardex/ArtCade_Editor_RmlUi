#include "editor-native/commands/sprite_presentation_commands.h"

#include "editor-native/model/numeric_validation.h"
#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kPresentationInvalidation =
    EditorInvalidation::Inspector | EditorInvalidation::Viewport;

DomainChange added(const ObjectTypeId& id, ComponentKind kind) {
    return DomainChange::objectTypeComponentAdded(id, kind);
}
DomainChange removed(const ObjectTypeId& id, ComponentKind kind) {
    return DomainChange::objectTypeComponentRemoved(id, kind);
}
DomainChange changed(const ObjectTypeId& id, ComponentKind kind) {
    return DomainChange::objectTypeComponentChanged(id, kind);
}

bool equal(const SpriteRendererOverride& lhs, const SpriteRendererOverride& rhs) {
    return lhs.imageAssetId == rhs.imageAssetId
        && lhs.visible == rhs.visible
        && lhs.capabilityEnabled == rhs.capabilityEnabled;
}

bool equal(const SpriteAnimatorOverride& lhs, const SpriteAnimatorOverride& rhs) {
    return lhs.animationAssetId == rhs.animationAssetId
        && lhs.defaultClipId == rhs.defaultClipId
        && lhs.autoPlay == rhs.autoPlay
        && lhs.playbackSpeed == rhs.playbackSpeed
        && lhs.capabilityEnabled == rhs.capabilityEnabled;
}

bool empty(const SpriteAnimatorOverride& value) {
    return !value.animationAssetId && !value.defaultClipId && !value.autoPlay
        && !value.playbackSpeed && !value.capabilityEnabled;
}

bool clipBelongsTo(const SpriteAnimationAssetDef& asset, const std::string& clipId) {
    return std::any_of(asset.clips.begin(), asset.clips.end(),
        [&](const SpriteAnimationClipDef& clip) { return clip.id == clipId; });
}

SceneInstanceDef* findInstance(ProjectDoc& document, const SceneId& sceneId,
                               EntityId entityId) {
    const auto scene = document.scenes.find(sceneId);
    if (scene == document.scenes.end()) return nullptr;
    const auto instance = std::find_if(scene->second.instances.begin(),
        scene->second.instances.end(),
        [&](const SceneInstanceDef& value) { return value.id == entityId; });
    return instance == scene->second.instances.end() ? nullptr : &*instance;
}

const SceneInstanceDef* selectedInstance(const ProjectDocument& document,
                                         const SceneId& sceneId, EntityId entityId) {
    return document.findInstanceInScene(sceneId, entityId);
}

std::string defaultClipId(const SpriteAnimationAssetDef& asset) {
    return asset.clips.empty() ? std::string{} : asset.clips.front().id;
}

std::size_t animationActionCount(const EntityDef& type) {
    if (!type.logicBoard) return 0;
    std::size_t count = 0;
    for (const LogicRuleDef& rule : type.logicBoard->rules) {
        for (const LogicBlockDef& action : rule.actions) {
            if (action.typeId == "animation.play_clip"
                || action.typeId == "animation.stop"
                || action.typeId == "animation.set_playback_speed"
                || action.typeId == "animation.set_speed") {
                ++count;
            }
        }
    }
    return count;
}

std::string requiredByBoardMessage(const ObjectTypeId& objectTypeId, std::size_t count) {
    return "Cannot remove Sprite Animator from \"" + objectTypeId
        + "\". Its Logic Board contains " + std::to_string(count)
        + (count == 1 ? std::string{" action that requires"}
                      : std::string{" actions that require"})
        + " animation playback.";
}
} // namespace

AddSpriteRendererToObjectTypeCommand::AddSpriteRendererToObjectTypeCommand(
    ObjectTypeId objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult AddSpriteRendererToObjectTypeCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type) return EditorOperationResult::failure("Unknown object type: " + objectTypeId_);
    if (type->spriteRenderer) return EditorOperationResult::success(EditorInvalidation::None);
    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).spriteRenderer = SpriteRendererComponent{};
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, added(objectTypeId_, ComponentKind::SpriteRenderer));
}

EditorOperationResult AddSpriteRendererToObjectTypeCommand::undo(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || !type->second.spriteRenderer
        || type->second.spriteAnimator) {
        return EditorOperationResult::failure("Cannot undo Object Type SpriteRenderer add");
    }
    type->second.spriteRenderer.reset();
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, removed(objectTypeId_, ComponentKind::SpriteRenderer));
}

RemoveSpriteRendererFromObjectTypeCommand::RemoveSpriteRendererFromObjectTypeCommand(
    ObjectTypeId objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult RemoveSpriteRendererFromObjectTypeCommand::apply(
    ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type || !type->spriteRenderer) {
        return EditorOperationResult::failure("Object Type has no SpriteRenderer");
    }
    const std::size_t requiredActions = animationActionCount(*type);
    if (type->spriteAnimator && requiredActions > 0) {
        return EditorOperationResult::failure(
            requiredByBoardMessage(objectTypeId_, requiredActions));
    }
    if (!captured_) {
        renderer_ = *type->spriteRenderer;
        animator_ = type->spriteAnimator;
        for (const auto& [sceneId, scene] : document.data().scenes) {
            for (const SceneInstanceDef& instance : scene.instances) {
                if (instance.objectTypeId != objectTypeId_) continue;
                instanceStates_.push_back(InstanceState{
                    sceneId, instance.id,
                    instance.spriteRendererOverride, instance.spriteAnimatorOverride});
            }
        }
        captured_ = true;
    }
    ProjectDoc staged = document.data();
    EntityDef& stagedType = staged.objectTypes.at(objectTypeId_);
    stagedType.spriteRenderer.reset();
    stagedType.spriteAnimator.reset();
    for (auto& [_, scene] : staged.scenes) {
        for (SceneInstanceDef& instance : scene.instances) {
            if (instance.objectTypeId != objectTypeId_) continue;
            instance.spriteRendererOverride.reset();
            instance.spriteAnimatorOverride.reset();
        }
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, removed(objectTypeId_, ComponentKind::SpriteRenderer));
}

EditorOperationResult RemoveSpriteRendererFromObjectTypeCommand::undo(
    ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || type->second.spriteRenderer) {
        return EditorOperationResult::failure("Cannot restore Object Type SpriteRenderer");
    }
    type->second.spriteRenderer = renderer_;
    type->second.spriteAnimator = animator_;
    for (const InstanceState& state : instanceStates_) {
        SceneInstanceDef* instance = findInstance(staged, state.sceneId, state.entityId);
        if (!instance || instance->objectTypeId != objectTypeId_) {
            return EditorOperationResult::failure("Cannot restore instance sprite overrides");
        }
        instance->spriteRendererOverride = state.renderer;
        instance->spriteAnimatorOverride = state.animator;
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, added(objectTypeId_, ComponentKind::SpriteRenderer));
}

SetObjectTypeSpriteSourceCommand::SetObjectTypeSpriteSourceCommand(
    ObjectTypeId objectTypeId, ObjectTypeSpriteSourceKind kind, AssetId assetId)
    : objectTypeId_(std::move(objectTypeId)), kind_(kind), assetId_(std::move(assetId)) {}

EditorOperationResult SetObjectTypeSpriteSourceCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type || !type->spriteRenderer) {
        return EditorOperationResult::failure("Object Type has no SpriteRenderer");
    }
    const SpriteAnimationAssetDef* animation = nullptr;
    if (kind_ == ObjectTypeSpriteSourceKind::Image
        && (assetId_.empty() || !document.hasImageAsset(assetId_))) {
        return EditorOperationResult::failure("Unknown image asset: " + assetId_);
    }
    if (kind_ == ObjectTypeSpriteSourceKind::Animation) {
        animation = document.findSpriteAnimationAsset(assetId_);
        if (!animation || animation->clips.empty()) {
            return EditorOperationResult::failure("Animation source has no resolvable clip");
        }
    }

    SpriteRendererComponent next = *type->spriteRenderer;
    next.imageAssetId.clear();
    if (kind_ == ObjectTypeSpriteSourceKind::Image) next.imageAssetId = assetId_;

    const AssetId previousAnimationId = type->spriteAnimator
        ? type->spriteAnimator->animationAssetId : AssetId{};
    const AssetId nextAnimationId =
        kind_ == ObjectTypeSpriteSourceKind::Animation ? assetId_ : AssetId{};
    const bool sameSource = next.imageAssetId == type->spriteRenderer->imageAssetId
        && nextAnimationId == previousAnimationId;
    if (sameSource) return EditorOperationResult::success(EditorInvalidation::None);

    if (!captured_) {
        previousRenderer_ = *type->spriteRenderer;
        previousAnimator_ = type->spriteAnimator;
        for (const auto& [sceneId, scene] : document.data().scenes) {
            for (const SceneInstanceDef& instance : scene.instances) {
                if (instance.objectTypeId == objectTypeId_) {
                    previousAnimatorOverrides_.push_back(AnimatorOverrideState{
                        sceneId, instance.id, instance.spriteAnimatorOverride});
                }
            }
        }
        captured_ = true;
    }

    ProjectDoc staged = document.data();
    EntityDef& stagedType = staged.objectTypes.at(objectTypeId_);
    stagedType.spriteRenderer = next;
    if (kind_ == ObjectTypeSpriteSourceKind::Animation) {
        if (!stagedType.spriteAnimator) {
            SpriteAnimatorComponent animator;
            animator.animationAssetId = assetId_;
            animator.defaultClipId = defaultClipId(*animation);
            stagedType.spriteAnimator = std::move(animator);
        } else {
            stagedType.spriteAnimator->animationAssetId = assetId_;
            if (stagedType.spriteAnimator->defaultClipId.empty()
                || !clipBelongsTo(*animation, stagedType.spriteAnimator->defaultClipId)) {
                stagedType.spriteAnimator->defaultClipId = defaultClipId(*animation);
            }
        }
        for (auto& [_, scene] : staged.scenes) {
            for (SceneInstanceDef& instance : scene.instances) {
                if (instance.objectTypeId != objectTypeId_ || !instance.spriteAnimatorOverride
                    || !instance.spriteAnimatorOverride->defaultClipId) continue;
                if (!clipBelongsTo(*animation,
                                   *instance.spriteAnimatorOverride->defaultClipId)) {
                    instance.spriteAnimatorOverride->defaultClipId.reset();
                    if (empty(*instance.spriteAnimatorOverride)) {
                        instance.spriteAnimatorOverride.reset();
                    }
                }
            }
        }
    } else {
        stagedType.spriteAnimator.reset();
        for (auto& [_, scene] : staged.scenes) {
            for (SceneInstanceDef& instance : scene.instances) {
                if (instance.objectTypeId == objectTypeId_) {
                    instance.spriteAnimatorOverride.reset();
                }
            }
        }
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteRenderer));
}

EditorOperationResult SetObjectTypeSpriteSourceCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || !type->second.spriteRenderer) {
        return EditorOperationResult::failure("Cannot restore Object Type sprite source");
    }
    type->second.spriteRenderer = previousRenderer_;
    type->second.spriteAnimator = previousAnimator_;
    for (const AnimatorOverrideState& state : previousAnimatorOverrides_) {
        SceneInstanceDef* instance = findInstance(staged, state.sceneId, state.entityId);
        if (!instance || instance->objectTypeId != objectTypeId_) {
            return EditorOperationResult::failure("Cannot restore animator override");
        }
        instance->spriteAnimatorOverride = state.animator;
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteRenderer));
}

AddSpriteAnimatorToObjectTypeCommand::AddSpriteAnimatorToObjectTypeCommand(
    ObjectTypeId objectTypeId, SpriteAnimatorComponent component)
    : objectTypeId_(std::move(objectTypeId)), component_(std::move(component)) {}

EditorOperationResult AddSpriteAnimatorToObjectTypeCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type || !type->spriteRenderer) {
        return EditorOperationResult::failure("SpriteAnimator requires an Object Type SpriteRenderer");
    }
    if (type->spriteAnimator) return EditorOperationResult::success(EditorInvalidation::None);
    if (!NumericValidation::isValid(component_)) {
        return EditorOperationResult::failure("SpriteAnimator playback speed must be positive");
    }
    if (component_.animationAssetId.empty()) {
        return EditorOperationResult::failure("SpriteAnimator requires an animation asset");
    }
    const SpriteAnimationAssetDef* asset =
        document.findSpriteAnimationAsset(component_.animationAssetId);
    if (!asset || !clipBelongsTo(*asset, component_.defaultClipId)) {
        return EditorOperationResult::failure("Default clip must belong to the animation asset");
    }
    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).spriteAnimator = component_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, added(objectTypeId_, ComponentKind::SpriteAnimator));
}

EditorOperationResult AddSpriteAnimatorToObjectTypeCommand::undo(ProjectDocument& document) {
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || !type->second.spriteAnimator) {
        return EditorOperationResult::failure("Cannot undo Object Type SpriteAnimator add");
    }
    type->second.spriteAnimator.reset();
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, removed(objectTypeId_, ComponentKind::SpriteAnimator));
}

RemoveSpriteAnimatorFromObjectTypeCommand::RemoveSpriteAnimatorFromObjectTypeCommand(
    ObjectTypeId objectTypeId)
    : objectTypeId_(std::move(objectTypeId)) {}

EditorOperationResult RemoveSpriteAnimatorFromObjectTypeCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type || !type->spriteAnimator) {
        return EditorOperationResult::failure("Object Type has no SpriteAnimator");
    }
    const std::size_t requiredActions = animationActionCount(*type);
    if (requiredActions > 0) {
        return EditorOperationResult::failure(
            requiredByBoardMessage(objectTypeId_, requiredActions));
    }
    if (!captured_) {
        removed_ = *type->spriteAnimator;
        for (const auto& [sceneId, scene] : document.data().scenes) {
            for (const SceneInstanceDef& instance : scene.instances) {
                if (instance.objectTypeId == objectTypeId_) {
                    instanceStates_.push_back(InstanceState{
                        sceneId, instance.id, instance.spriteAnimatorOverride});
                }
            }
        }
        captured_ = true;
    }
    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).spriteAnimator.reset();
    for (auto& [_, scene] : staged.scenes) {
        for (SceneInstanceDef& instance : scene.instances) {
            if (instance.objectTypeId == objectTypeId_) {
                instance.spriteAnimatorOverride.reset();
            }
        }
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, removed(objectTypeId_, ComponentKind::SpriteAnimator));
}

EditorOperationResult RemoveSpriteAnimatorFromObjectTypeCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || type->second.spriteAnimator) {
        return EditorOperationResult::failure("Cannot restore Object Type SpriteAnimator");
    }
    type->second.spriteAnimator = removed_;
    for (const InstanceState& state : instanceStates_) {
        SceneInstanceDef* instance = findInstance(staged, state.sceneId, state.entityId);
        if (!instance || instance->objectTypeId != objectTypeId_) {
            return EditorOperationResult::failure("Cannot restore animator override");
        }
        instance->spriteAnimatorOverride = state.animator;
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, added(objectTypeId_, ComponentKind::SpriteAnimator));
}

SetObjectTypeInitialClipCommand::SetObjectTypeInitialClipCommand(
    ObjectTypeId objectTypeId, std::string clipId)
    : objectTypeId_(std::move(objectTypeId)), next_(std::move(clipId)) {}

EditorOperationResult SetObjectTypeInitialClipCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type || !type->spriteRenderer || !type->spriteAnimator) {
        return EditorOperationResult::failure("Object Type SpriteAnimator is missing");
    }
    const SpriteAnimationAssetDef* asset =
        document.findSpriteAnimationAsset(type->spriteAnimator->animationAssetId);
    if (!asset || !clipBelongsTo(*asset, next_)) {
        return EditorOperationResult::failure("Default clip must belong to the animation asset");
    }
    if (type->spriteAnimator->defaultClipId == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) { previous_ = type->spriteAnimator->defaultClipId; captured_ = true; }
    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).spriteAnimator->defaultClipId = next_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteAnimator));
}

EditorOperationResult SetObjectTypeInitialClipCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || !type->second.spriteAnimator) {
        return EditorOperationResult::failure("Cannot restore default clip");
    }
    type->second.spriteAnimator->defaultClipId = previous_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteAnimator));
}

SetObjectTypeAutoPlayCommand::SetObjectTypeAutoPlayCommand(
    ObjectTypeId objectTypeId, bool autoPlay)
    : objectTypeId_(std::move(objectTypeId)), next_(autoPlay) {}

EditorOperationResult SetObjectTypeAutoPlayCommand::apply(ProjectDocument& document) {
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type || !type->spriteAnimator) {
        return EditorOperationResult::failure("Object Type SpriteAnimator is missing");
    }
    if (type->spriteAnimator->autoPlay == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) { previous_ = type->spriteAnimator->autoPlay; captured_ = true; }
    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).spriteAnimator->autoPlay = next_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteAnimator));
}

EditorOperationResult SetObjectTypeAutoPlayCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || !type->second.spriteAnimator) {
        return EditorOperationResult::failure("Cannot restore Object Type Auto Play");
    }
    type->second.spriteAnimator->autoPlay = previous_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteAnimator));
}

SetObjectTypePlaybackSpeedCommand::SetObjectTypePlaybackSpeedCommand(
    ObjectTypeId objectTypeId, float speed)
    : objectTypeId_(std::move(objectTypeId)), next_(speed) {}

EditorOperationResult SetObjectTypePlaybackSpeedCommand::apply(ProjectDocument& document) {
    if (!NumericValidation::isPositive(next_)) {
        return EditorOperationResult::failure("Playback speed must be positive");
    }
    const EntityDef* type = document.findObjectType(objectTypeId_);
    if (!type || !type->spriteAnimator) {
        return EditorOperationResult::failure("Object Type SpriteAnimator is missing");
    }
    if (type->spriteAnimator->playbackSpeed == next_) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) { previous_ = type->spriteAnimator->playbackSpeed; captured_ = true; }
    ProjectDoc staged = document.data();
    staged.objectTypes.at(objectTypeId_).spriteAnimator->playbackSpeed = next_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteAnimator));
}

EditorOperationResult SetObjectTypePlaybackSpeedCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    const auto type = staged.objectTypes.find(objectTypeId_);
    if (type == staged.objectTypes.end() || !type->second.spriteAnimator) {
        return EditorOperationResult::failure("Cannot restore Object Type playback speed");
    }
    type->second.spriteAnimator->playbackSpeed = previous_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation, changed(objectTypeId_, ComponentKind::SpriteAnimator));
}

SetInstanceSpriteOverrideCommand::SetInstanceSpriteOverrideCommand(
    SceneId sceneId, EntityId entityId, SpriteRendererOverride value)
    : sceneId_(std::move(sceneId)), entityId_(entityId), next_(std::move(value)) {}

EditorOperationResult SetInstanceSpriteOverrideCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* instance = selectedInstance(document, sceneId_, entityId_);
    if (!instance) return EditorOperationResult::failure("Selected instance is missing");
    const EntityDef* type = document.findObjectType(instance->objectTypeId);
    if (!type || !type->spriteRenderer) {
        return EditorOperationResult::failure("Object Type has no SpriteRenderer");
    }
    if (next_.capabilityEnabled) {
        return EditorOperationResult::failure("Capability state is migration-only");
    }
    if (instance->spriteRendererOverride && equal(*instance->spriteRendererOverride, next_)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    SceneInstanceDef projected = *instance;
    projected.spriteRendererOverride = next_;
    const ResolvedSpritePresentation resolved = resolveSpritePresentation(*type, projected);
    if (!resolved.renderer) return EditorOperationResult::failure("SpriteRenderer override is invalid");
    if (!resolved.renderer->imageAssetId.empty()
        && !document.hasImageAsset(resolved.renderer->imageAssetId)) {
        return EditorOperationResult::failure("Sprite override references a missing image asset");
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *instance)) {
            return EditorOperationResult::failure("Cannot override SpriteRenderer: layer is locked");
        }
        previous_ = instance->spriteRendererOverride;
        captured_ = true;
    }
    ProjectDoc staged = document.data();
    findInstance(staged, sceneId_, entityId_)->spriteRendererOverride = next_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteRenderer));
}

EditorOperationResult SetInstanceSpriteOverrideCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, entityId_);
    if (!instance) return EditorOperationResult::failure("Cannot restore SpriteRenderer override");
    instance->spriteRendererOverride = previous_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteRenderer));
}

SetInstanceAnimatorOverrideCommand::SetInstanceAnimatorOverrideCommand(
    SceneId sceneId, EntityId entityId, SpriteAnimatorOverride value)
    : sceneId_(std::move(sceneId)), entityId_(entityId), next_(std::move(value)) {}

EditorOperationResult SetInstanceAnimatorOverrideCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* instance = selectedInstance(document, sceneId_, entityId_);
    if (!instance) return EditorOperationResult::failure("Selected instance is missing");
    const EntityDef* type = document.findObjectType(instance->objectTypeId);
    if (!type || !type->spriteAnimator || !type->spriteRenderer) {
        return EditorOperationResult::failure("Object Type has no SpriteAnimator");
    }
    if (next_.capabilityEnabled) {
        return EditorOperationResult::failure("Capability state is migration-only");
    }
    if (next_.playbackSpeed && !NumericValidation::isPositive(*next_.playbackSpeed)) {
        return EditorOperationResult::failure("Playback speed must be positive");
    }
    SceneInstanceDef projected = *instance;
    projected.spriteAnimatorOverride = next_;
    const ResolvedSpritePresentation resolved = resolveSpritePresentation(*type, projected);
    if (!resolved.animator || !resolved.renderer
        || resolved.animator->animationAssetId.empty()) {
        return EditorOperationResult::failure("SpriteAnimator override has no animation source");
    }
    if (!document.hasSpriteAnimationAsset(resolved.animator->animationAssetId)) {
        return EditorOperationResult::failure("SpriteAnimator override references a missing animation asset");
    }
    const SpriteAnimationAssetDef* asset =
        document.findSpriteAnimationAsset(resolved.animator->animationAssetId);
    if (!asset || !clipBelongsTo(*asset, resolved.animator->defaultClipId)) {
        return EditorOperationResult::failure("Default clip must belong to the animation asset");
    }
    if (instance->spriteAnimatorOverride && equal(*instance->spriteAnimatorOverride, next_)) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *instance)) {
            return EditorOperationResult::failure("Cannot override SpriteAnimator: layer is locked");
        }
        previous_ = instance->spriteAnimatorOverride;
        captured_ = true;
    }
    ProjectDoc staged = document.data();
    findInstance(staged, sceneId_, entityId_)->spriteAnimatorOverride = next_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteAnimator));
}

EditorOperationResult SetInstanceAnimatorOverrideCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, entityId_);
    if (!instance) return EditorOperationResult::failure("Cannot restore SpriteAnimator override");
    instance->spriteAnimatorOverride = previous_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteAnimator));
}

ClearInstanceSpriteOverrideCommand::ClearInstanceSpriteOverrideCommand(
    SceneId sceneId, EntityId entityId)
    : sceneId_(std::move(sceneId)), entityId_(entityId) {}

EditorOperationResult ClearInstanceSpriteOverrideCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* instance = selectedInstance(document, sceneId_, entityId_);
    if (!instance || !instance->spriteRendererOverride) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *instance)) {
            return EditorOperationResult::failure("Cannot reset SpriteRenderer override: layer is locked");
        }
        removed_ = *instance->spriteRendererOverride;
        captured_ = true;
    }
    ProjectDoc staged = document.data();
    findInstance(staged, sceneId_, entityId_)->spriteRendererOverride.reset();
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteRenderer));
}

EditorOperationResult ClearInstanceSpriteOverrideCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, entityId_);
    if (!instance) return EditorOperationResult::failure("Cannot restore SpriteRenderer override");
    instance->spriteRendererOverride = removed_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteRenderer));
}

ClearInstanceAnimatorOverrideCommand::ClearInstanceAnimatorOverrideCommand(
    SceneId sceneId, EntityId entityId)
    : sceneId_(std::move(sceneId)), entityId_(entityId) {}

EditorOperationResult ClearInstanceAnimatorOverrideCommand::apply(ProjectDocument& document) {
    const SceneInstanceDef* instance = selectedInstance(document, sceneId_, entityId_);
    if (!instance || !instance->spriteAnimatorOverride) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        if (document.isInstanceLayerLocked(sceneId_, *instance)) {
            return EditorOperationResult::failure("Cannot reset SpriteAnimator override: layer is locked");
        }
        removed_ = *instance->spriteAnimatorOverride;
        captured_ = true;
    }
    ProjectDoc staged = document.data();
    findInstance(staged, sceneId_, entityId_)->spriteAnimatorOverride.reset();
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteAnimator));
}

EditorOperationResult ClearInstanceAnimatorOverrideCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Nothing to restore");
    ProjectDoc staged = document.data();
    SceneInstanceDef* instance = findInstance(staged, sceneId_, entityId_);
    if (!instance) return EditorOperationResult::failure("Cannot restore SpriteAnimator override");
    instance->spriteAnimatorOverride = removed_;
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kPresentationInvalidation,
        DomainChange::componentChanged(sceneId_, entityId_, ComponentKind::SpriteAnimator));
}

} // namespace ArtCade::EditorNative
