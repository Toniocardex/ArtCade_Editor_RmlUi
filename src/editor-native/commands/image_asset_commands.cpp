#include "editor-native/commands/image_asset_commands.h"

#include "editor-native/model/project_document.h"

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
        removed_ = *current;
        captured_ = true;
    }
    // Capture every sprite renderer pointing at this image before it goes, so the
    // clear is exact and reversible. Do this once; redo reuses the same list.
    if (!refsCaptured_) {
        for (const auto& [sceneId, scene] : document.data().scenes) {
            for (const SceneInstanceDef& instance : scene.instances) {
                if (instance.spriteRenderer
                    && instance.spriteRenderer->imageAssetId == assetId_) {
                    clearedRefs_.emplace_back(sceneId, instance.id);
                }
            }
        }
        refsCaptured_ = true;
    }
    for (const auto& [sceneId, entityId] : clearedRefs_) {
        document.setSpriteRendererAsset(sceneId, entityId, AssetId{});   // "" = no image
    }
    if (!document.removeImageAsset(assetId_)) {
        return EditorOperationResult::failure("Failed to remove image asset");
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveImageAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addImageAsset(removed_)) {
        return EditorOperationResult::failure("Cannot undo image asset removal");
    }
    // Re-point every renderer we cleared back at the restored image.
    for (const auto& [sceneId, entityId] : clearedRefs_) {
        document.setSpriteRendererAsset(sceneId, entityId, assetId_);
    }
    return EditorOperationResult::success(kRemoveInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
