#include "editor-native/commands/sprite_animation_commands.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_animation_references.h"

#include "logic-core.h"
#include "sprite-animation-core.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>
#include <variant>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kAssetInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Viewport | EditorInvalidation::Inspector;

const SpriteAnimationClipDef* findClip(const SpriteAnimationAssetDef& asset,
                                       const std::string& clipId) {
    for (const SpriteAnimationClipDef& clip : asset.clips) {
        if (clip.id == clipId) return &clip;
    }
    return nullptr;
}

bool validFrame(const SpriteFrameDef& frame) {
    return !frame.id.empty() && frame.width > 0 && frame.height > 0
        && frame.x >= 0 && frame.y >= 0;
}

} // namespace

CreateSpriteAnimationAssetCommand::CreateSpriteAnimationAssetCommand(
    AssetId assetId, std::string name, std::string clipId,
    std::string clipName, AssetId imageId)
    : assetId_(std::move(assetId)), name_(std::move(name)),
      clipId_(std::move(clipId)), clipName_(std::move(clipName)),
      imageId_(std::move(imageId)) {}

EditorOperationResult CreateSpriteAnimationAssetCommand::apply(ProjectDocument& document) {
    if (assetId_.empty() || name_.empty() || clipId_.empty() || clipName_.empty()) {
        return EditorOperationResult::failure("Sprite animation asset is incomplete");
    }
    if (!document.hasImageAsset(imageId_)) {
        return EditorOperationResult::failure("Sprite animation source image is missing");
    }
    if (document.hasSpriteAnimationAsset(assetId_)) {
        return EditorOperationResult::failure("Sprite animation asset already exists");
    }

    SpriteAnimationClipDef clip;
    clip.id = clipId_;
    clip.name = clipName_;

    SpriteAnimationAssetDef asset;
    asset.id = assetId_;
    asset.name = name_;
    asset.sourceImageAssetId = imageId_;
    asset.clips.push_back(std::move(clip));

    ProjectDoc staged = document.data();
    staged.spriteAnimationAssets.push_back(std::move(asset));
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(
        kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult CreateSpriteAnimationAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeSpriteAnimationAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo sprite animation asset creation");
    }
    return EditorOperationResult::success(
        kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

AddSpriteAnimationAssetCommand::AddSpriteAnimationAssetCommand(
    AssetId assetId, std::string name)
    : assetId_(std::move(assetId)), name_(std::move(name)) {}

EditorOperationResult AddSpriteAnimationAssetCommand::apply(ProjectDocument& document) {
    if (assetId_.empty() || name_.empty()) {
        return EditorOperationResult::failure("Sprite animation asset is incomplete");
    }
    SpriteAnimationAssetDef asset;
    asset.id = assetId_;
    asset.name = name_;
    if (!document.addSpriteAnimationAsset(std::move(asset))) {
        return EditorOperationResult::failure("Failed to add sprite animation asset");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult AddSpriteAnimationAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeSpriteAnimationAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo sprite animation asset add");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

RemoveSpriteAnimationAssetCommand::RemoveSpriteAnimationAssetCommand(AssetId assetId)
    : assetId_(std::move(assetId)) {}

EditorOperationResult RemoveSpriteAnimationAssetCommand::apply(ProjectDocument& document) {
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    if (!asset) return EditorOperationResult::failure("Unknown sprite animation asset");
    if (!captured_) {
        const auto& assets = document.data().spriteAnimationAssets;
        const auto assetIt = std::find_if(
            assets.begin(), assets.end(), [&](const SpriteAnimationAssetDef& candidate) {
                return candidate.id == assetId_;
            });
        assetIndex_ = static_cast<std::size_t>(std::distance(assets.begin(), assetIt));
        removed_ = *asset;
        const std::vector<AnimationAssetReference> refs =
            collectAnimationAssetReferences(document, assetId_);
        for (const AnimationAssetReference& ref : refs) {
            if (ref.kind == AnimationReferenceKind::ObjectTypeAnimator) {
                const EntityDef* type = document.findObjectType(ref.objectTypeId);
                if (type && type->spriteAnimator) {
                    clearedTypeRefs_.push_back(
                        ClearedTypeRef{ref.objectTypeId, *type->spriteAnimator});
                }
            } else if (ref.kind == AnimationReferenceKind::InstanceAnimatorOverride) {
                const SceneInstanceDef* instance =
                    document.findInstanceInScene(ref.sceneId, ref.entityId);
                if (!instance) continue;
                const bool explicitRef = instance->spriteAnimatorOverride
                    && instance->spriteAnimatorOverride->animationAssetId
                    && *instance->spriteAnimatorOverride->animationAssetId == assetId_;
                clearedOverrideRefs_.push_back(ClearedOverrideRef{
                    ref.sceneId, ref.entityId, instance->spriteAnimatorOverride, explicitRef});
            } else if (ref.kind == AnimationReferenceKind::LogicPlayClip) {
                const EntityDef* type = document.findObjectType(ref.objectTypeId);
                if (!type || !type->logicBoard) continue;
                for (const LogicRuleDef& rule : type->logicBoard->rules) {
                    if (rule.id != ref.ruleId) continue;
                    if (ref.actionIndex >= rule.actions.size()) continue;
                    const LogicBlockDef& action = rule.actions[ref.actionIndex];
                    ClearedLogicRef cleared{ref.objectTypeId, ref.ruleId, ref.actionIndex};
                    if (const LogicPropertyDef* assetProp =
                            Logic::findProperty(action, "animationAssetId")) {
                        if (const auto* value =
                                std::get_if<LogicAssetReference>(&assetProp->value)) {
                            cleared.previousAnimationAssetId = value->id;
                        }
                    }
                    if (const LogicPropertyDef* clipProp =
                            Logic::findProperty(action, "clipId")) {
                        if (const auto* value =
                                std::get_if<LogicStringValue>(&clipProp->value)) {
                            cleared.previousClipId = value->value;
                        }
                    }
                    clearedLogicRefs_.push_back(std::move(cleared));
                }
            }
        }
        // Match RemoveSpriteAnimatorFromObjectType: when the OT animator goes,
        // wipe every instance override for that type (inherited clip/speed/
        // autoPlay leftovers are not in collectAnimationAssetReferences).
        for (const ClearedTypeRef& typeRef : clearedTypeRefs_) {
            for (const auto& [sceneId, scene] : document.data().scenes) {
                for (const SceneInstanceDef& instance : scene.instances) {
                    if (instance.objectTypeId != typeRef.objectTypeId) continue;
                    if (!instance.spriteAnimatorOverride) continue;
                    const bool already = std::any_of(
                        clearedOverrideRefs_.begin(), clearedOverrideRefs_.end(),
                        [&](const ClearedOverrideRef& ref) {
                            return ref.sceneId == sceneId && ref.entityId == instance.id;
                        });
                    if (already) continue;
                    clearedOverrideRefs_.push_back(ClearedOverrideRef{
                        sceneId, instance.id, instance.spriteAnimatorOverride, false});
                }
            }
        }
        captured_ = true;
    }

    ProjectDoc staged = document.data();
    auto assetIt = std::find_if(
        staged.spriteAnimationAssets.begin(), staged.spriteAnimationAssets.end(),
        [&](const SpriteAnimationAssetDef& candidate) { return candidate.id == assetId_; });
    if (assetIt == staged.spriteAnimationAssets.end()) {
        return EditorOperationResult::failure("Failed to stage sprite animation asset removal");
    }
    staged.spriteAnimationAssets.erase(assetIt);
    for (const ClearedTypeRef& ref : clearedTypeRefs_) {
        auto typeIt = staged.objectTypes.find(ref.objectTypeId);
        if (typeIt != staged.objectTypes.end()) typeIt->second.spriteAnimator.reset();
    }
    for (const ClearedOverrideRef& ref : clearedOverrideRefs_) {
        auto& instances = staged.scenes.at(ref.sceneId).instances;
        auto instance = std::find_if(instances.begin(), instances.end(),
            [&](const SceneInstanceDef& value) { return value.id == ref.entityId; });
        if (instance == instances.end()) {
            return EditorOperationResult::failure("Failed to stage animation reference removal");
        }
        if (ref.clearExplicitAnimation && instance->spriteAnimatorOverride) {
            instance->spriteAnimatorOverride->animationAssetId = AssetId{};
        }
        instance->spriteAnimatorOverride.reset();
    }
    for (const ClearedLogicRef& ref : clearedLogicRefs_) {
        auto typeIt = staged.objectTypes.find(ref.objectTypeId);
        if (typeIt == staged.objectTypes.end() || !typeIt->second.logicBoard) {
            return EditorOperationResult::failure("Failed to stage Logic animation reference removal");
        }
        for (LogicRuleDef& rule : typeIt->second.logicBoard->rules) {
            if (rule.id != ref.ruleId) continue;
            if (ref.actionIndex >= rule.actions.size()) {
                return EditorOperationResult::failure("Failed to stage Logic animation reference removal");
            }
            LogicBlockDef& action = rule.actions[ref.actionIndex];
            for (LogicPropertyDef& property : action.properties) {
                if (property.key == "animationAssetId") {
                    property.value = LogicAssetReference{};
                } else if (property.key == "clipId") {
                    property.value = LogicStringValue{};
                }
            }
        }
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveSpriteAnimationAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || document.hasSpriteAnimationAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo sprite animation asset removal");
    }

    ProjectDoc staged = document.data();
    if (assetIndex_ > staged.spriteAnimationAssets.size()) {
        return EditorOperationResult::failure("Cannot restore sprite animation asset order");
    }
    staged.spriteAnimationAssets.insert(
        staged.spriteAnimationAssets.begin() + static_cast<std::ptrdiff_t>(assetIndex_), removed_);

    for (const ClearedTypeRef& ref : clearedTypeRefs_) {
        const auto typeIt = staged.objectTypes.find(ref.objectTypeId);
        if (typeIt == staged.objectTypes.end()) {
            return EditorOperationResult::failure("Cannot restore animation reference: object type missing");
        }
        typeIt->second.spriteAnimator = ref.animator;
    }
    for (const ClearedOverrideRef& ref : clearedOverrideRefs_) {
        const auto sceneIt = staged.scenes.find(ref.sceneId);
        if (sceneIt == staged.scenes.end()) {
            return EditorOperationResult::failure("Cannot restore animation reference: scene missing");
        }
        auto instanceIt = std::find_if(
            sceneIt->second.instances.begin(), sceneIt->second.instances.end(),
            [&](const SceneInstanceDef& instance) { return instance.id == ref.entityId; });
        if (instanceIt == sceneIt->second.instances.end()) {
            return EditorOperationResult::failure("Cannot restore animation reference: instance missing");
        }
        instanceIt->spriteAnimatorOverride = ref.animator;
    }
    for (const ClearedLogicRef& ref : clearedLogicRefs_) {
        auto typeIt = staged.objectTypes.find(ref.objectTypeId);
        if (typeIt == staged.objectTypes.end() || !typeIt->second.logicBoard) {
            return EditorOperationResult::failure("Cannot restore Logic animation reference");
        }
        for (LogicRuleDef& rule : typeIt->second.logicBoard->rules) {
            if (rule.id != ref.ruleId) continue;
            if (ref.actionIndex >= rule.actions.size()) {
                return EditorOperationResult::failure("Cannot restore Logic animation reference");
            }
            LogicBlockDef& action = rule.actions[ref.actionIndex];
            for (LogicPropertyDef& property : action.properties) {
                if (property.key == "animationAssetId") {
                    property.value = LogicAssetReference{ref.previousAnimationAssetId};
                } else if (property.key == "clipId") {
                    property.value = LogicStringValue{ref.previousClipId};
                }
            }
        }
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

AddAnimationClipCommand::AddAnimationClipCommand(
    AssetId assetId, std::string clipId, std::string name)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), name_(std::move(name)) {}

EditorOperationResult AddAnimationClipCommand::apply(ProjectDocument& document) {
    SpriteAnimationClipDef clip;
    clip.id = clipId_;
    clip.name = name_;
    if (!document.addAnimationClip(assetId_, std::move(clip))) {
        return EditorOperationResult::failure("Failed to add animation clip");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult AddAnimationClipCommand::undo(ProjectDocument& document) {
    if (!document.removeAnimationClip(assetId_, clipId_)) {
        return EditorOperationResult::failure("Cannot undo animation clip add");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

RenameAnimationClipCommand::RenameAnimationClipCommand(
    AssetId assetId, std::string clipId, std::string name)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), next_(std::move(name)) {}

EditorOperationResult RenameAnimationClipCommand::apply(ProjectDocument& document) {
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    const SpriteAnimationClipDef* clip = asset ? findClip(*asset, clipId_) : nullptr;
    if (!clip) return EditorOperationResult::failure("Unknown animation clip");
    if (clip->name == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = clip->name;
        captured_ = true;
    }
    if (!document.renameAnimationClip(assetId_, clipId_, next_)) {
        return EditorOperationResult::failure("Failed to rename animation clip");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RenameAnimationClipCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.renameAnimationClip(assetId_, clipId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo animation clip rename");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

RemoveAnimationClipCommand::RemoveAnimationClipCommand(AssetId assetId, std::string clipId)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)) {}

EditorOperationResult RemoveAnimationClipCommand::apply(ProjectDocument& document) {
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    const SpriteAnimationClipDef* clip = asset ? findClip(*asset, clipId_) : nullptr;
    if (!clip) return EditorOperationResult::failure("Unknown animation clip");
    if (!collectAnimationClipReferences(document, assetId_, clipId_).empty()) {
        return EditorOperationResult::failure("Animation clip is still referenced");
    }
    if (!captured_) {
        removed_ = *clip;
        captured_ = true;
    }
    if (!document.removeAnimationClip(assetId_, clipId_)) {
        return EditorOperationResult::failure("Failed to remove animation clip");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveAnimationClipCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addAnimationClip(assetId_, removed_)) {
        return EditorOperationResult::failure("Cannot undo animation clip removal");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

ReplaceAnimationFramesCommand::ReplaceAnimationFramesCommand(
    AssetId assetId, std::vector<SpriteFrameDef> frames)
    : assetId_(std::move(assetId)), next_(std::move(frames)) {}

EditorOperationResult ReplaceAnimationFramesCommand::apply(ProjectDocument& document) {
    if (!std::all_of(next_.begin(), next_.end(), validFrame)) {
        return EditorOperationResult::failure("Animation frame rectangles must be positive");
    }
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    if (!asset) return EditorOperationResult::failure("Unknown sprite animation asset");
    if (asset->frames == next_) {
        const bool sequencesEmpty = std::all_of(
            asset->clips.begin(), asset->clips.end(),
            [](const SpriteAnimationClipDef& clip) { return clip.frameIds.empty(); });
        if (sequencesEmpty) return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previousFrames_ = asset->frames;
        previousSequences_.clear();
        previousSequences_.reserve(asset->clips.size());
        for (const SpriteAnimationClipDef& clip : asset->clips) {
            previousSequences_.push_back(ClipFrameIds{clip.id, clip.frameIds});
        }
        captured_ = true;
    }
    if (!document.replaceAnimationFrames(assetId_, next_)) {
        return EditorOperationResult::failure("Failed to replace animation frames");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult ReplaceAnimationFramesCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.replaceAnimationFrames(assetId_, previousFrames_)) {
        return EditorOperationResult::failure("Cannot undo animation frame change");
    }
    for (const ClipFrameIds& sequence : previousSequences_) {
        if (!document.setAnimationClipFrameIds(assetId_, sequence.clipId, sequence.frameIds)) {
            return EditorOperationResult::failure("Cannot undo animation frame sequences");
        }
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

ReplaceAnimationSourceImageCommand::ReplaceAnimationSourceImageCommand(
    AssetId assetId, AssetId imageId)
    : assetId_(std::move(assetId)), nextImageId_(std::move(imageId)) {}

EditorOperationResult ReplaceAnimationSourceImageCommand::apply(ProjectDocument& document) {
    if (!document.hasImageAsset(nextImageId_)) {
        return EditorOperationResult::failure("Sprite animation source image is missing");
    }
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    if (!asset) return EditorOperationResult::failure("Unknown sprite animation asset");
    const bool sequencesEmpty = std::all_of(
        asset->clips.begin(), asset->clips.end(),
        [](const SpriteAnimationClipDef& clip) { return clip.frameIds.empty(); });
    if (asset->sourceImageAssetId == nextImageId_ && asset->frames.empty() && sequencesEmpty) {
        return EditorOperationResult::success(EditorInvalidation::None);
    }
    if (!captured_) {
        previousImageId_ = asset->sourceImageAssetId;
        previousFrames_ = asset->frames;
        previousSequences_.clear();
        previousSequences_.reserve(asset->clips.size());
        for (const SpriteAnimationClipDef& clip : asset->clips) {
            previousSequences_.push_back(ClipFrameIds{clip.id, clip.frameIds});
        }
        captured_ = true;
    }
    if (!document.replaceAnimationSourceImage(assetId_, nextImageId_)) {
        return EditorOperationResult::failure("Failed to replace animation source image");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult ReplaceAnimationSourceImageCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.replaceAnimationSourceImage(assetId_, previousImageId_)) {
        return EditorOperationResult::failure("Cannot undo animation source image change");
    }
    if (!document.replaceAnimationFrames(assetId_, previousFrames_)) {
        return EditorOperationResult::failure("Cannot undo animation source frames");
    }
    for (const ClipFrameIds& sequence : previousSequences_) {
        if (!document.setAnimationClipFrameIds(assetId_, sequence.clipId, sequence.frameIds)) {
            return EditorOperationResult::failure("Cannot undo animation source sequences");
        }
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

DuplicateSpriteAnimationAssetCommand::DuplicateSpriteAnimationAssetCommand(
    AssetId sourceAssetId, AssetId newAssetId, std::string newName)
    : sourceAssetId_(std::move(sourceAssetId)), newAssetId_(std::move(newAssetId)),
      newName_(std::move(newName)) {}

EditorOperationResult DuplicateSpriteAnimationAssetCommand::apply(ProjectDocument& document) {
    if (newAssetId_.empty() || newName_.empty()) {
        return EditorOperationResult::failure("Duplicated sprite animation asset is incomplete");
    }
    if (document.hasSpriteAnimationAsset(newAssetId_)) {
        return EditorOperationResult::failure("Sprite animation asset already exists");
    }
    const SpriteAnimationAssetDef* source = document.findSpriteAnimationAsset(sourceAssetId_);
    if (!source) return EditorOperationResult::failure("Unknown sprite animation asset");

    std::unordered_map<SpriteFrameId, SpriteFrameId> frameRemap;
    SpriteAnimationAssetDef copy;
    copy.id = newAssetId_;
    copy.name = newName_;
    copy.sourceImageAssetId = source->sourceImageAssetId;
    copy.frames.reserve(source->frames.size());
    for (const SpriteFrameDef& frame : source->frames) {
        SpriteFrameDef remapped = frame;
        remapped.id = newAssetId_ + ":" + frame.id;
        frameRemap.emplace(frame.id, remapped.id);
        copy.frames.push_back(std::move(remapped));
    }
    copy.clips.reserve(source->clips.size());
    for (const SpriteAnimationClipDef& clip : source->clips) {
        SpriteAnimationClipDef remapped = clip;
        remapped.id = newAssetId_ + ":" + clip.id;
        remapped.name = clip.name;
        remapped.frameIds.clear();
        remapped.frameIds.reserve(clip.frameIds.size());
        for (const SpriteFrameId& frameId : clip.frameIds) {
            const auto it = frameRemap.find(frameId);
            if (it == frameRemap.end()) {
                return EditorOperationResult::failure(
                    "Cannot duplicate animation: frameId missing from pool");
            }
            remapped.frameIds.push_back(it->second);
        }
        for (const SpriteAnimationClipDef& existing : copy.clips) {
            if (existing.name == remapped.name) {
                remapped.name += " Copy";
            }
        }
        copy.clips.push_back(std::move(remapped));
    }

    if (!document.addSpriteAnimationAsset(std::move(copy))) {
        return EditorOperationResult::failure("Failed to duplicate sprite animation asset");
    }
    return EditorOperationResult::success(
        kAssetInvalidation, DomainChange::assetChanged(newAssetId_));
}

EditorOperationResult DuplicateSpriteAnimationAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeSpriteAnimationAsset(newAssetId_)) {
        return EditorOperationResult::failure("Cannot undo sprite animation asset duplicate");
    }
    return EditorOperationResult::success(
        kAssetInvalidation, DomainChange::assetChanged(newAssetId_));
}

DuplicateAnimationClipCommand::DuplicateAnimationClipCommand(
    AssetId assetId, std::string sourceClipId, std::string newClipId, std::string newName)
    : assetId_(std::move(assetId)), sourceClipId_(std::move(sourceClipId)),
      newClipId_(std::move(newClipId)), newName_(std::move(newName)) {}

EditorOperationResult DuplicateAnimationClipCommand::apply(ProjectDocument& document) {
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    const SpriteAnimationClipDef* source = asset ? findClip(*asset, sourceClipId_) : nullptr;
    if (!source) return EditorOperationResult::failure("Unknown animation clip");
    SpriteAnimationClipDef copy = *source;
    copy.id = newClipId_;
    copy.name = newName_;
    if (!document.addAnimationClip(assetId_, std::move(copy))) {
        return EditorOperationResult::failure("Failed to duplicate animation clip");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult DuplicateAnimationClipCommand::undo(ProjectDocument& document) {
    if (!document.removeAnimationClip(assetId_, newClipId_)) {
        return EditorOperationResult::failure("Cannot undo animation clip duplicate");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

SetAnimationClipFrameIdsCommand::SetAnimationClipFrameIdsCommand(
    AssetId assetId, std::string clipId, std::vector<SpriteFrameId> frameIds)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), next_(std::move(frameIds)) {}

EditorOperationResult SetAnimationClipFrameIdsCommand::apply(ProjectDocument& document) {
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    const SpriteAnimationClipDef* clip = asset ? findClip(*asset, clipId_) : nullptr;
    if (!clip) return EditorOperationResult::failure("Unknown animation clip");
    for (const SpriteFrameId& frameId : next_) {
        bool found = false;
        for (const SpriteFrameDef& frame : asset->frames) {
            if (frame.id == frameId) { found = true; break; }
        }
        if (!found) {
            return EditorOperationResult::failure(
                "Animation clip frameIds must resolve against the asset frame pool");
        }
    }
    if (clip->frameIds == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = clip->frameIds;
        captured_ = true;
    }
    if (!document.setAnimationClipFrameIds(assetId_, clipId_, next_)) {
        return EditorOperationResult::failure("Failed to set animation clip frameIds");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult SetAnimationClipFrameIdsCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setAnimationClipFrameIds(assetId_, clipId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo animation clip frameIds");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

SetAnimationClipFrameRateCommand::SetAnimationClipFrameRateCommand(
    AssetId assetId, std::string clipId, float fps)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), next_(fps) {}

EditorOperationResult SetAnimationClipFrameRateCommand::apply(ProjectDocument& document) {
    if (!Animation::isValidAnimationFps(next_)) {
        return EditorOperationResult::failure("Animation FPS must be positive");
    }
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    const SpriteAnimationClipDef* clip = asset ? findClip(*asset, clipId_) : nullptr;
    if (!clip) return EditorOperationResult::failure("Unknown animation clip");
    if (clip->framesPerSecond == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = clip->framesPerSecond;
        captured_ = true;
    }
    if (!document.setAnimationClipFrameRate(assetId_, clipId_, next_)) {
        return EditorOperationResult::failure("Failed to set animation FPS");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult SetAnimationClipFrameRateCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setAnimationClipFrameRate(assetId_, clipId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo animation FPS change");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

SetAnimationClipPlaybackModeCommand::SetAnimationClipPlaybackModeCommand(
    AssetId assetId, std::string clipId, AnimationPlaybackMode mode)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), next_(mode) {}

EditorOperationResult SetAnimationClipPlaybackModeCommand::apply(ProjectDocument& document) {
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    const SpriteAnimationClipDef* clip = asset ? findClip(*asset, clipId_) : nullptr;
    if (!clip) return EditorOperationResult::failure("Unknown animation clip");
    if (clip->playbackMode == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = clip->playbackMode;
        captured_ = true;
    }
    if (!document.setAnimationClipPlaybackMode(assetId_, clipId_, next_)) {
        return EditorOperationResult::failure("Failed to set animation playback mode");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult SetAnimationClipPlaybackModeCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setAnimationClipPlaybackMode(assetId_, clipId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo animation playback mode change");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
