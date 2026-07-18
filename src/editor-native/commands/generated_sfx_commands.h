#pragma once

#include "artcade/sfx/types.hpp"
#include "core/types.h"
#include "editor-native/commands/editor_command.h"
#include "editor-native/model/generated_sfx_policy.h"

#include <string>

namespace ArtCade::EditorNative {

// Persistent Generated SFX mutations. Preview and render jobs are deliberately
// absent: they are workspace/application concerns and never enter Undo.
class CreateGeneratedSfxCommand final : public EditorCommand {
public:
    CreateGeneratedSfxCommand(std::string id, std::string name,
                              artcade::sfx::SfxRecipe recipe = {});
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "CreateGeneratedSfx"; }
private:
    std::string id_;
    std::string name_;
    artcade::sfx::SfxRecipe recipe_{};
    ProjectDoc before_{};
    ProjectDoc after_{};
    bool captured_ = false;
};

class RenameGeneratedSfxCommand final : public EditorCommand {
public:
    RenameGeneratedSfxCommand(std::string id, std::string name);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RenameGeneratedSfx"; }
private:
    std::string id_;
    std::string name_;
    ProjectDoc before_{};
    ProjectDoc after_{};
    bool captured_ = false;
};

class UpdateGeneratedSfxRecipeCommand final : public EditorCommand {
public:
    UpdateGeneratedSfxRecipeCommand(std::string id, artcade::sfx::SfxRecipe recipe);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "UpdateGeneratedSfxRecipe"; }
private:
    std::string id_;
    artcade::sfx::SfxRecipe recipe_{};
    ProjectDoc before_{};
    ProjectDoc after_{};
    bool captured_ = false;
};

class RemoveGeneratedSfxCommand final : public EditorCommand {
public:
    explicit RemoveGeneratedSfxCommand(std::string id);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RemoveGeneratedSfx"; }
private:
    std::string id_;
    ProjectDoc before_{};
    ProjectDoc after_{};
    bool captured_ = false;
};

/** Copies recipe + name into a new identity. Never copies outputAssetId/path. */
class DuplicateGeneratedSfxCommand final : public EditorCommand {
public:
    DuplicateGeneratedSfxCommand(std::string sourceId, std::string newId, std::string newName);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "DuplicateGeneratedSfx"; }
private:
    std::string sourceId_;
    std::string newId_;
    std::string newName_;
    ProjectDoc before_{};
    ProjectDoc after_{};
    bool captured_ = false;
};

/** First generate or regenerate: upsert the single AudioAssetDef for this recipe.
 *  1 GeneratedSfxDef ↔ 1 AudioAssetDef ↔ 1 WAV. Never allocates serial outputs. */
class RegisterGeneratedSfxOutputCommand final : public EditorCommand {
public:
    RegisterGeneratedSfxOutputCommand(std::string id,
                                      artcade::sfx::SfxRecipe expectedRecipe,
                                      AudioAssetDef outputAsset);
    EditorOperationResult apply(ProjectDocument& document) override;
    EditorOperationResult undo(ProjectDocument& document) override;
    const char* name() const override { return "RegisterGeneratedSfxOutput"; }
private:
    std::string id_;
    artcade::sfx::SfxRecipe expectedRecipe_{};
    AudioAssetDef outputAsset_{};
    ProjectDoc before_{};
    ProjectDoc after_{};
    bool captured_ = false;
};

} // namespace ArtCade::EditorNative
