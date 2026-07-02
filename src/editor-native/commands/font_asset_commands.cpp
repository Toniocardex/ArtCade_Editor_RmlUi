#include "editor-native/commands/font_asset_commands.h"

#include "editor-native/model/project_document.h"

#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kInvalidation = EditorInvalidation::Assets;
} // namespace

AddFontAssetCommand::AddFontAssetCommand(AssetId assetId, std::string sourcePath,
                                         int defaultPixelSize, FontGlyphPreset glyphPreset)
    : assetId_(std::move(assetId)), sourcePath_(std::move(sourcePath)),
      defaultPixelSize_(defaultPixelSize), glyphPreset_(glyphPreset) {}

EditorOperationResult AddFontAssetCommand::apply(ProjectDocument& document) {
    if (assetId_.empty() || sourcePath_.empty()) {
        return EditorOperationResult::failure("Font asset needs an id and a source path");
    }
    if (defaultPixelSize_ <= 0) {
        return EditorOperationResult::failure("Font default pixel size must be positive");
    }
    if (document.hasFontAsset(assetId_)) {
        return EditorOperationResult::failure("Font asset already exists: " + assetId_);
    }
    FontAssetDef asset;
    asset.assetId = assetId_;
    asset.name = assetId_;
    asset.sourcePath = sourcePath_;
    asset.defaultPixelSize = defaultPixelSize_;
    asset.glyphPreset = glyphPreset_;
    if (!document.addFontAsset(asset)) {
        return EditorOperationResult::failure("Failed to add font asset");
    }
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult AddFontAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeFontAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo font asset add");
    }
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

RemoveFontAssetCommand::RemoveFontAssetCommand(AssetId assetId)
    : assetId_(std::move(assetId)) {}

EditorOperationResult RemoveFontAssetCommand::apply(ProjectDocument& document) {
    const FontAssetDef* current = document.findFontAsset(assetId_);
    if (!current) return EditorOperationResult::failure("Unknown font asset: " + assetId_);
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
    }
    if (!document.removeFontAsset(assetId_)) {
        return EditorOperationResult::failure("Failed to remove font asset");
    }
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveFontAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addFontAsset(removed_)) {
        return EditorOperationResult::failure("Cannot undo font asset removal");
    }
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
