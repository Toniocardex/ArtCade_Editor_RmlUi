#include "editor-native/commands/sprite_animation_commands.h"

#include "editor-native/model/project_document.h"

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

bool animationAssetReferenced(const ProjectDocument& document, const AssetId& assetId) {
    for (const auto& [_, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (instance.spriteRenderer
                && instance.spriteRenderer->animationAssetId == assetId) {
                return true;
            }
        }
    }
    return false;
}

bool clipReferenced(const ProjectDocument& document, const AssetId& assetId,
                    const std::string& clipId) {
    for (const auto& [_, scene] : document.data().scenes) {
        for (const SceneInstanceDef& instance : scene.instances) {
            if (!instance.spriteRenderer || !instance.spriteAnimator) continue;
            if (instance.spriteRenderer->animationAssetId == assetId
                && instance.spriteAnimator->initialClipId == clipId) {
                return true;
            }
        }
    }
    return false;
}

} // namespace

AddSpriteAnimationAssetCommand::AddSpriteAnimationAssetCommand(
    AssetId assetId, AssetId imageId, std::string name)
    : assetId_(std::move(assetId)), imageId_(std::move(imageId)), name_(std::move(name)) {}

EditorOperationResult AddSpriteAnimationAssetCommand::apply(ProjectDocument& document) {
    if (assetId_.empty() || imageId_.empty() || name_.empty()) {
        return EditorOperationResult::failure("Sprite animation asset is incomplete");
    }
    if (!document.hasImageAsset(imageId_)) {
        return EditorOperationResult::failure("Sprite animation references a missing image asset");
    }
    SpriteAnimationAssetDef asset;
    asset.id = assetId_;
    asset.name = name_;
    asset.imageId = imageId_;
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
    if (animationAssetReferenced(document, assetId_)) {
        return EditorOperationResult::failure("Sprite animation asset is still referenced");
    }
    if (!captured_) {
        removed_ = *asset;
        captured_ = true;
    }
    if (!document.removeSpriteAnimationAsset(assetId_)) {
        return EditorOperationResult::failure("Failed to remove sprite animation asset");
    }
    return EditorOperationResult::success(kAssetInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveSpriteAnimationAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addSpriteAnimationAsset(removed_)) {
        return EditorOperationResult::failure("Cannot undo sprite animation asset removal");
    }
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
