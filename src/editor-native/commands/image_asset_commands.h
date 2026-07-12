#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <cstddef>
#include <optional>
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
    struct ClearedRef {
        SceneId sceneId;
        EntityId entityId = INVALID_ENTITY;
        SpriteRendererComponent renderer{};
        std::optional<SpriteAnimatorComponent> animator;
    };
    AssetId      assetId_;
    ImageAssetDef removed_{};   // captured for an exact undo
    std::size_t  assetIndex_ = 0;
    bool         captured_ = false;
    // Sprite renderers that referenced this image. Removing the asset clears them
    // (delete means delete — no dangling source on the entity); undo restores the
    // exact reference. Captured once, reused across redo.
    std::vector<ClearedRef> clearedRefs_;
};

} // namespace ArtCade::EditorNative
