#include "editor-native/commands/audio_asset_commands.h"

#include "editor-native/model/project_document.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kInvalidation = EditorInvalidation::Assets;
} // namespace

AddAudioAssetCommand::AddAudioAssetCommand(AssetId assetId, std::string sourcePath,
                                           AudioLoadMode loadMode)
    : assetId_(std::move(assetId)), sourcePath_(std::move(sourcePath)), loadMode_(loadMode) {}

EditorOperationResult AddAudioAssetCommand::apply(ProjectDocument& document) {
    if (assetId_.empty() || sourcePath_.empty()) {
        return EditorOperationResult::failure("Audio asset needs an id and a source path");
    }
    if (document.hasAudioAsset(assetId_)) {
        return EditorOperationResult::failure("Audio asset already exists: " + assetId_);
    }
    AudioAssetDef asset;
    asset.assetId = assetId_;
    asset.name = assetId_;
    asset.sourcePath = sourcePath_;
    asset.loadMode = loadMode_;
    if (!document.addAudioAsset(asset)) {
        return EditorOperationResult::failure("Failed to add audio asset");
    }
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult AddAudioAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeAudioAsset(assetId_)) {
        return EditorOperationResult::failure("Cannot undo audio asset add");
    }
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

RemoveAudioAssetCommand::RemoveAudioAssetCommand(AssetId assetId)
    : assetId_(std::move(assetId)) {}

EditorOperationResult RemoveAudioAssetCommand::apply(ProjectDocument& document) {
    const AudioAssetDef* current = document.findAudioAsset(assetId_);
    if (!current) return EditorOperationResult::failure("Unknown audio asset: " + assetId_);
    if (!captured_) {
        before_ = document.data();
        after_ = before_;
        after_.audioAssets.erase(std::remove_if(
            after_.audioAssets.begin(), after_.audioAssets.end(),
            [&](const AudioAssetDef& asset) { return asset.assetId == assetId_; }),
            after_.audioAssets.end());
        for (artcade::sfx::GeneratedSfxDef& definition : after_.generatedSfx) {
            if (definition.outputAssetId == assetId_) {
                definition.outputAssetId.clear();
                definition.outputPath.clear();
                definition.generatedRecipeFingerprint.clear();
            }
        }
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveAudioAssetCommand::undo(ProjectDocument& document) {
    if (!captured_) {
        return EditorOperationResult::failure("Cannot undo audio asset removal");
    }
    document.commitStagedCommand(before_);
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative
