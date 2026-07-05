#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>
#include <utility>
#include <vector>

namespace ArtCade::EditorNative {

// Image asset catalog commands. The file copy on disk is an application concern
// (editor_app): these only mutate ProjectDoc.imageAssets, with a portable
// relative sourcePath. Undo of an add does not delete the copied file.
class AddImageAssetCommand final : public EditorCommand {
public:
    AddImageAssetCommand(AssetId assetId, std::string sourcePath);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddImageAsset"; }

private:
    AssetId     assetId_;
    std::string sourcePath_;
};

class RemoveImageAssetCommand final : public EditorCommand {
public:
    explicit RemoveImageAssetCommand(AssetId assetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveImageAsset"; }

private:
    AssetId      assetId_;
    ImageAssetDef removed_{};   // captured for an exact undo
    bool         captured_ = false;
    // Sprite renderers that referenced this image. Removing the asset clears them
    // (delete means delete — no dangling source on the entity); undo restores the
    // exact reference. Captured once, reused across redo.
    std::vector<std::pair<SceneId, EntityId>> clearedRefs_;
    bool         refsCaptured_ = false;
};

} // namespace ArtCade::EditorNative
