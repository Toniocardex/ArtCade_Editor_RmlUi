#include "editor-native/commands/script_asset_commands.h"

#include "editor-native/model/project_document.h"

#include <utility>

namespace ArtCade::EditorNative {

namespace {
constexpr EditorInvalidation kInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Inspector;
}

AddScriptAssetCommand::AddScriptAssetCommand(
    AssetId assetId, std::string name, std::string sourcePath) {
    asset_.assetId = std::move(assetId);
    asset_.name = std::move(name);
    asset_.sourcePath = std::move(sourcePath);
}

EditorOperationResult AddScriptAssetCommand::apply(ProjectDocument& document) {
    if (document.hasScriptAsset(asset_.assetId)) {
        return EditorOperationResult::failure("Script asset already exists: " + asset_.assetId);
    }
    if (!document.addScriptAsset(asset_)) {
        return EditorOperationResult::failure(
            "Script asset needs a unique id, name and project-relative .lua path");
    }
    return EditorOperationResult::success(
        kInvalidation, DomainChange::assetChanged(asset_.assetId));
}

EditorOperationResult AddScriptAssetCommand::undo(ProjectDocument& document) {
    if (!document.removeScriptAsset(asset_.assetId)) {
        return EditorOperationResult::failure("Cannot undo script asset add");
    }
    return EditorOperationResult::success(
        kInvalidation, DomainChange::assetChanged(asset_.assetId));
}

RemoveScriptAssetCommand::RemoveScriptAssetCommand(AssetId assetId)
    : assetId_(std::move(assetId)) {}

EditorOperationResult RemoveScriptAssetCommand::apply(ProjectDocument& document) {
    const ScriptAssetDef* current = document.findScriptAsset(assetId_);
    if (!current) return EditorOperationResult::failure("Unknown script asset: " + assetId_);
    if (!captured_) {
        removed_ = *current;
        captured_ = true;
    }
    if (!document.removeScriptAsset(assetId_)) {
        return EditorOperationResult::failure("Failed to remove script asset");
    }
    return EditorOperationResult::success(
        kInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RemoveScriptAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.addScriptAsset(removed_)) {
        return EditorOperationResult::failure("Cannot undo script asset removal");
    }
    return EditorOperationResult::success(
        kInvalidation, DomainChange::assetChanged(assetId_));
}

RenameScriptAssetCommand::RenameScriptAssetCommand(AssetId assetId, std::string name)
    : assetId_(std::move(assetId)), name_(std::move(name)) {}

EditorOperationResult RenameScriptAssetCommand::apply(ProjectDocument& document) {
    const ScriptAssetDef* current = document.findScriptAsset(assetId_);
    if (!current) return EditorOperationResult::failure("Unknown script asset: " + assetId_);
    if (name_.empty()) return EditorOperationResult::failure("Script name cannot be empty");
    if (current->name == name_) return EditorOperationResult::success(EditorInvalidation::None);
    if (!captured_) {
        previousName_ = current->name;
        captured_ = true;
    }
    if (!document.setScriptAssetName(assetId_, name_)) {
        return EditorOperationResult::failure("Failed to rename script asset");
    }
    return EditorOperationResult::success(
        kInvalidation, DomainChange::assetChanged(assetId_));
}

EditorOperationResult RenameScriptAssetCommand::undo(ProjectDocument& document) {
    if (!captured_ || !document.setScriptAssetName(assetId_, previousName_)) {
        return EditorOperationResult::failure("Cannot undo script asset rename");
    }
    return EditorOperationResult::success(
        kInvalidation, DomainChange::assetChanged(assetId_));
}

} // namespace ArtCade::EditorNative

