#include "editor-native/commands/sprite_animation_commands.h"

#include "editor-native/model/project_document.h"
#include "editor-native/model/sprite_render_view.h"

#include <algorithm>
#include <cmath>
#include <utility>

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

bool validFrame(const SpriteAnimationFrameDef& frame) {
    return frame.width > 0 && frame.height > 0 && frame.x >= 0 && frame.y >= 0;
}

bool clipReferenced(const ProjectDocument& document, const AssetId& assetId,
                    const std::string& clipId) {
    for (const auto& [_, type] : document.data().objectTypes) {
        if (type.spriteRenderer && type.spriteAnimator
            && type.spriteRenderer->animationAssetId == assetId
            && type.spriteAnimator->initialClipId == clipId) {
            return true;
        }
    }
    for (const auto& [_, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            const auto type = document.data().objectTypes.find(instance.objectTypeId);
            if (type == document.data().objectTypes.end()) continue;
            const ResolvedSpritePresentation presentation =
                resolveSpritePresentation(type->second, instance);
            if (presentation.renderer && presentation.animator
                && presentation.renderer->animationAssetId == assetId
                && presentation.animator->initialClipId == clipId) {
                return true;
            }
        }
    }
    return false;
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
    clip.imageId = imageId_;

    SpriteAnimationAssetDef asset;
    asset.id = assetId_;
    asset.name = name_;
    asset.defaultClipId = clipId_;
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
        for (const auto& [objectTypeId, type] : document.data().objectTypes) {
            if (type.spriteRenderer && type.spriteRenderer->animationAssetId == assetId_) {
                clearedTypeRefs_.push_back(
                    ClearedTypeRef{objectTypeId, *type.spriteRenderer, type.spriteAnimator});
            }
        }
        for (const auto& [sceneId, scene] : document.data().scenes) {
            for (const SceneInstanceDef& instance : scene.instances) {
                const auto type = document.data().objectTypes.find(instance.objectTypeId);
                const bool inherited = type != document.data().objectTypes.end()
                    && type->second.spriteRenderer
                    && type->second.spriteRenderer->animationAssetId == assetId_
                    && (!instance.spriteRendererOverride
                        || !instance.spriteRendererOverride->animationAssetId);
                const bool explicitRef = instance.spriteRendererOverride
                    && instance.spriteRendererOverride->animationAssetId == assetId_;
                if (!inherited && !explicitRef) continue;
                clearedOverrideRefs_.push_back(ClearedOverrideRef{
                    sceneId, instance.id, instance.spriteRendererOverride,
                    instance.spriteAnimatorOverride, explicitRef});
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
    for (auto& [_, type] : staged.objectTypes) {
        if (type.spriteRenderer && type.spriteRenderer->animationAssetId == assetId_) {
            type.spriteRenderer->animationAssetId.clear();
            type.spriteAnimator.reset();
        }
    }
    for (const ClearedOverrideRef& ref : clearedOverrideRefs_) {
        auto& instances = staged.scenes.at(ref.sceneId).instances;
        auto instance = std::find_if(instances.begin(), instances.end(),
            [&](const SceneInstanceDef& value) { return value.id == ref.entityId; });
        if (instance == instances.end()) {
            return EditorOperationResult::failure("Failed to stage animation reference removal");
        }
        if (ref.clearExplicitAnimation && instance->spriteRendererOverride) {
            instance->spriteRendererOverride->animationAssetId = AssetId{};
        }
        instance->spriteAnimatorOverride.reset();
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
        if (typeIt == staged.objectTypes.end() || !typeIt->second.spriteRenderer) {
            return EditorOperationResult::failure("Cannot restore animation reference: object type missing");
        }
        typeIt->second.spriteRenderer = ref.renderer;
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
        instanceIt->spriteRendererOverride = ref.renderer;
        instanceIt->spriteAnimatorOverride = ref.animator;
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

AddAnimationClipCommand::AddAnimationClipCommand(
    AssetId assetId, std::string clipId, std::string name, AssetId imageId)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), name_(std::move(name)),
      imageId_(std::move(imageId)) {}

EditorOperationResult AddAnimationClipCommand::apply(ProjectDocument& document) {
    SpriteAnimationClipDef clip;
    clip.id = clipId_;
    clip.name = name_;
    clip.imageId = imageId_;
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
    if (clipReferenced(document, assetId_, clipId_)) {
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

SetAnimationClipFramesCommand::SetAnimationClipFramesCommand(
    AssetId assetId, std::string clipId, std::vector<SpriteAnimationFrameDef> frames)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), next_(std::move(frames)) {}

EditorOperationResult SetAnimationClipFramesCommand::apply(ProjectDocument& document) {
    if (!std::all_of(next_.begin(), next_.end(), validFrame)) {
        return EditorOperationResult::failure("Animation frame rectangles must be positive");
    }
    const SpriteAnimationAssetDef* asset = document.findSpriteAnimationAsset(assetId_);
    const SpriteAnimationClipDef* clip = asset ? findClip(*asset, clipId_) : nullptr;
    if (!clip) return EditorOperationResult::failure("Unknown animation clip");
    if (clip->frames == next_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previous_ = clip->frames;
        captured_ = true;
    }
    if (!document.setAnimationClipFrames(assetId_, clipId_, next_)) {
        return EditorOperationResult::failure("Failed to set animation frames");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult SetAnimationClipFramesCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setAnimationClipFrames(assetId_, clipId_, previous_)) {
        return EditorOperationResult::failure("Cannot undo animation frame change");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

SetAnimationClipFrameRateCommand::SetAnimationClipFrameRateCommand(
    AssetId assetId, std::string clipId, float fps)
    : assetId_(std::move(assetId)), clipId_(std::move(clipId)), next_(fps) {}

EditorOperationResult SetAnimationClipFrameRateCommand::apply(ProjectDocument& document) {
    if (!std::isfinite(next_) || next_ <= 0.f) {
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
