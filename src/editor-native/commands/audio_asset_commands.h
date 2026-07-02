#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// Audio catalog commands. The file copy is an application concern; these only
// mutate ProjectDoc.audioAssets with a portable relative sourcePath and the
// authored load mode. Undo of an add does not delete the copied file.
class AddAudioAssetCommand final : public EditorCommand {
public:
    AddAudioAssetCommand(AssetId assetId, std::string sourcePath, AudioLoadMode loadMode);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddAudioAsset"; }

private:
    AssetId       assetId_;
    std::string   sourcePath_;
    AudioLoadMode loadMode_;
};

class RemoveAudioAssetCommand final : public EditorCommand {
public:
    explicit RemoveAudioAssetCommand(AssetId assetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveAudioAsset"; }

private:
    AssetId       assetId_;
    AudioAssetDef removed_{};
    bool          captured_ = false;
};

} // namespace ArtCade::EditorNative
