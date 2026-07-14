#include "editor-native/commands/image_asset_commands.h"

#include "editor-native/model/project_document.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
// A new asset appears in the Assets panel and in the Inspector's sprite-asset
// list; removal can also leave a sprite reference dangling, so it touches the
// Viewport too.
constexpr EditorInvalidation kAddInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Inspector;
constexpr EditorInvalidation kRemoveInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Inspector | EditorInvalidation::Viewport;
} // namespace

AddImageAssetCommand::AddImageAssetCommand(AssetId assetId, std::string sourcePath)
    : assetId_(std::move(assetId)), sourcePath_(std::move(sourcePath)) {}

EditorOperationResult AddImageAssetCommand::apply(ProjectDocument& document) {
    if (assetId_.empty() || sourcePath_.empty()) {
        return EditorOperationResult::failure("Image asset needs an id and a source path");
    }
    if (document.hasImageAsset(assetId_)) {
        return EditorOperationResult::failure("Image asset already exists: " + assetId_);
    }
    ImageAssetDef asset;
    asset.assetId = assetId_;
    asset.name = assetId_;
    asset.sourcePath = sourcePath_;
    if (!document.addImageAsset(asset)) {
        return EditorOperationResult::failure("Failed to add image asset");
    }
    return EditorOperationResult::success(kAddInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult AddImageAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeImageAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo image asset add");
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

RemoveImageAssetCommand::RemoveImageAssetCommand(AssetId assetId)
    : assetId_(std::move(assetId)) {}

EditorOperationResult RemoveImageAssetCommand::apply(ProjectDocument& document) {
    const ImageAssetDef* current = document.findImageAsset(assetId_);
    if (!current) return EditorOperationResult::failure("Unknown image asset: " + assetId_);
    // An animation clip's sheet is this image: removing the image would orphan
    // the clip, so require the animation to go first (a structural parent, not a
    // loose reference). Sprite-renderer references below are cleared, not blocked.
    for (const SpriteAnimationAssetDef& animation : document.data().spriteAnimationAssets) {
        for (const SpriteAnimationClipDef& clip : animation.clips) {
            if (clip.imageId == assetId_) {
                return EditorOperationResult::failure(
                    "Image asset is used by a sprite animation asset - remove that first");
            }
        }
    }
    if (!captured_) {
        const auto& assets = document.data().imageAssets;
        const auto assetIt = std::find_if(
            assets.begin(), assets.end(), [&](const ImageAssetDef& candidate) {
                return candidate.assetId == assetId_;
            });
        assetIndex_ = static_cast<std::size_t>(std::distance(assets.begin(), assetIt));
        removed_ = *current;
        for (const auto& [objectTypeId, type] : document.data().objectTypes) {
            if (type.spriteRenderer && type.spriteRenderer->imageAssetId == assetId_) {
                clearedTypeRefs_.push_back(ClearedTypeRef{objectTypeId, *type.spriteRenderer});
            }
        }
        for (const auto& [sceneId, scene] : document.data().scenes) {
            for (const SceneInstanceDef& instance : scene.instances) {
                if (instance.spriteRendererOverride
                    && instance.spriteRendererOverride->imageAssetId == assetId_) {
                    clearedOverrideRefs_.push_back(ClearedOverrideRef{
                        sceneId, instance.id, *instance.spriteRendererOverride});
                }
            }
        }
        captured_ = true;
    }

    ProjectDoc staged = document.data();
    const auto assetIt = std::find_if(
        staged.imageAssets.begin(), staged.imageAssets.end(),
        [&](const ImageAssetDef& candidate) { return candidate.assetId == assetId_; });
    if (assetIt == staged.imageAssets.end()) {
        return EditorOperationResult::failure("Failed to stage image asset removal");
    }
    staged.imageAssets.erase(assetIt);
    for (auto& [_, type] : staged.objectTypes) {
        if (type.spriteRenderer && type.spriteRenderer->imageAssetId == assetId_) {
            type.spriteRenderer->imageAssetId.clear();
        }
    }
    for (auto& [_, scene] : staged.scenes) {
        for (SceneInstanceDef& instance : scene.instances) {
            if (instance.spriteRendererOverride
                && instance.spriteRendererOverride->imageAssetId == assetId_) {
                instance.spriteRendererOverride->imageAssetId = AssetId{};
            }
        }
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveImageAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || document.hasImageAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo image asset removal");
    }

    ProjectDoc staged = document.data();
    if (assetIndex_ > staged.imageAssets.size()) {
        return EditorOperationResult::failure("Cannot restore image asset order");
    }
    staged.imageAssets.insert(
        staged.imageAssets.begin() + static_cast<std::ptrdiff_t>(assetIndex_), removed_);
    for (const ClearedTypeRef& ref : clearedTypeRefs_) {
        const auto typeIt = staged.objectTypes.find(ref.objectTypeId);
        if (typeIt == staged.objectTypes.end() || !typeIt->second.spriteRenderer) {
            return EditorOperationResult::failure("Cannot restore image reference: object type missing");
        }
        typeIt->second.spriteRenderer = ref.renderer;
    }
    for (const ClearedOverrideRef& ref : clearedOverrideRefs_) {
        const auto sceneIt = staged.scenes.find(ref.sceneId);
        if (sceneIt == staged.scenes.end()) {
            return EditorOperationResult::failure("Cannot restore image reference: scene missing");
        }
        const auto instanceIt = std::find_if(
            sceneIt->second.instances.begin(), sceneIt->second.instances.end(),
            [&](const SceneInstanceDef& instance) { return instance.id == ref.entityId; });
        if (instanceIt == sceneIt->second.instances.end()) {
            return EditorOperationResult::failure("Cannot restore image reference: instance missing");
        }
        instanceIt->spriteRendererOverride = ref.renderer;
    }
    document.commitStagedCommand(std::move(staged));
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
