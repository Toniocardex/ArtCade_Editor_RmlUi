#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <string>

namespace ArtCade::EditorNative {

// Font catalog commands. The file copy is an application concern; these only
// mutate ProjectDoc.fontAssets with a portable relative sourcePath plus the
// authored default pixel size and glyph preset. Undo of an add does not delete
// the copied file.
class AddFontAssetCommand final : public EditorCommand {
public:
    AddFontAssetCommand(AssetId assetId, std::string sourcePath,
                        int defaultPixelSize, FontGlyphPreset glyphPreset);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "AddFontAsset"; }

private:
    AssetId         assetId_;
    std::string     sourcePath_;
    int             defaultPixelSize_;
    FontGlyphPreset glyphPreset_;
};

class RemoveFontAssetCommand final : public EditorCommand {
public:
    explicit RemoveFontAssetCommand(AssetId assetId);

    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveFontAsset"; }

private:
    AssetId      assetId_;
    FontAssetDef removed_{};
    bool         captured_ = false;
};

} // namespace ArtCade::EditorNative
