#pragma once

#include "artcade/sfx/types.hpp"
#include "core/types.h"
#include "editor-native/commands/editor_command.h"

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

// Commits a successfully encoded output only if the recipe snapshot is still
// current. It registers/updates the normal AudioAssetDef atomically with the
// recipe link. The encoder/file write has already completed outside the model.
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

bool generatedSfxRecipesEqual(const artcade::sfx::SfxRecipe& left,
                              const artcade::sfx::SfxRecipe& right);

} // namespace ArtCade::EditorNative
