#include "editor-native/commands/audio_asset_commands.h"

#include "editor-native/commands/generated_sfx_commands.h"
#include "editor-native/model/project_document.h"

#include <algorithm>
#include <optional>
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
    if (audioDisplayNameExists(document.data(), assetId_)) {
        return EditorOperationResult::failure("Audio name already exists: " + assetId_);
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

RenameAudioAssetCommand::RenameAudioAssetCommand(AssetId assetId, std::string name)
    : assetId_(std::move(assetId)), name_(std::move(name)) {}

EditorOperationResult RenameAudioAssetCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const std::string name = normalizeAudioDisplayName(name_);
        if (name.empty()) return EditorOperationResult::failure("Audio name cannot be empty");
        if (!document.hasAudioAsset(assetId_)) {
            return EditorOperationResult::failure("Unknown audio asset: " + assetId_);
        }
        if (audioIsLinkedGeneratedOutput(document.data(), assetId_)) {
            return EditorOperationResult::failure(
                "Rename the owning Generated SFX instead of its linked audio output");
        }
        const AudioAssetDef* current = document.findAudioAsset(assetId_);
        if (current && current->name == name)
            return EditorOperationResult::success(EditorInvalidation::None);
        if (audioDisplayNameExists(document.data(), name, std::nullopt, assetId_)) {
            return EditorOperationResult::failure("Audio name already exists: " + name);
        }
        before_ = document.data();
        after_ = before_;
        auto it = std::find_if(after_.audioAssets.begin(), after_.audioAssets.end(),
            [&](const AudioAssetDef& asset) { return asset.assetId == assetId_; });
        if (it == after_.audioAssets.end()) {
            return EditorOperationResult::failure("Unknown audio asset: " + assetId_);
        }
        it->name = name;
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RenameAudioAssetCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo audio asset rename");
    document.commitStagedCommand(before_);
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(assetId_));
}

RemoveAudioAssetCommand::RemoveAudioAssetCommand(AssetId assetId)
    : assetId_(std::move(assetId)) {}

EditorOperationResult RemoveAudioAssetCommand::apply(ProjectDocument& document) {
    const AudioAssetDef* current = document.findAudioAsset(assetId_);
    if (!current) return EditorOperationResult::failure("Unknown audio asset: " + assetId_);
    if (audioIsLinkedGeneratedOutput(document.data(), assetId_)) {
        return EditorOperationResult::failure(
            "Delete the owning Generated SFX instead of its linked audio output");
    }
    if (!captured_) {
        before_ = document.data();
        after_ = before_;
        after_.audioAssets.erase(std::remove_if(
            after_.audioAssets.begin(), after_.audioAssets.end(),
            [&](const AudioAssetDef& asset) { return asset.assetId == assetId_; }),
            after_.audioAssets.end());
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
