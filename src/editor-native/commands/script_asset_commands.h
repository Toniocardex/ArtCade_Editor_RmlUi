#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// Script catalog Commands mutate metadata only. Source-file creation/import and
// deletion are explicit application workflows and never hidden inside Undo.
class AddScriptAssetCommand final : public EditorCommand {
public:
    AddScriptAssetCommand(AssetId assetId, std::string name, std::string sourcePath);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddScriptAsset"; }

private:
    ScriptAssetDef asset_;
};

class RemoveScriptAssetCommand final : public EditorCommand {
public:
    explicit RemoveScriptAssetCommand(AssetId assetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveScriptAsset"; }

private:
    AssetId        assetId_;
    ScriptAssetDef removed_{};
    bool            captured_ = false;
};

class RenameScriptAssetCommand final : public EditorCommand {
public:
    RenameScriptAssetCommand(AssetId assetId, std::string name);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameScriptAsset"; }

private:
    AssetId     assetId_;
    std::string name_;
    std::string previousName_;
    bool        captured_ = false;
};

} // namespace ArtCade::EditorNative

