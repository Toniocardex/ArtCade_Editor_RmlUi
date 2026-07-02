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
    for (const SpriteAnimationAssetDef& animation : document.data().spriteAnimationAssets) {
        if (animation.imageId == assetId_) {
            return EditorOperationResult::failure(
                "Image asset is used by a sprite animation asset");
        }
    }
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
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
    return EditorOperationResult::success(kAddInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
