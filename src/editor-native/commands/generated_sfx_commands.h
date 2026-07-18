#pragma once

#include "artcade/sfx/types.hpp"
#include "core/types.h"
#include "editor-native/commands/editor_command.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

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

bool generatedSfxRecipesEqual(const artcade::sfx::SfxRecipe& left,
                              const artcade::sfx::SfxRecipe& right);

/** Derived readiness of a Generated SFX versus its linked WAV (fingerprint model). */
enum class GeneratedSfxOutputStatus {
    NeedsGeneration, // no linked output (or catalog entry missing)
    Stale,           // output exists; recipe fingerprint differs from last generate
    Ready,           // output exists; fingerprint matches current recipe
};

[[nodiscard]] GeneratedSfxOutputStatus generatedSfxOutputStatus(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition);

[[nodiscard]] const char* generatedSfxOutputStatusLabel(GeneratedSfxOutputStatus status);

/** Case-insensitive display-name collision across Generated SFX + Audio assets. */
[[nodiscard]] bool audioDisplayNameExists(
    const ProjectDoc& document,
    std::string_view candidate,
    const std::optional<std::string>& exceptSfxId = std::nullopt,
    const std::optional<AssetId>& exceptAudioAssetId = std::nullopt);

/** Next free `generated-sfx-N` that is not blocked by a leftover audio asset id. */
[[nodiscard]] std::string nextGeneratedSfxId(const ProjectDocument& document);
/** Unique display name: base, then "base 02", "base 03", … across the audio namespace. */
[[nodiscard]] std::string uniqueGeneratedSfxName(const ProjectDocument& document,
                                                 const std::string& baseName);

/** True when any AudioAssetDef already owns this project-relative path. */
[[nodiscard]] bool generatedSfxOutputPathTaken(const ProjectDocument& document,
                                               const std::string& relativePath,
                                               const std::optional<AssetId>& exceptAssetId = std::nullopt);

/** Deterministic 1:1 catalog identity: generated-audio-{sfxId}. */
[[nodiscard]] AssetId generatedAudioAssetId(const std::string& generatedSfxId);
[[nodiscard]] std::string generatedAudioRelativePath(const std::string& generatedSfxId);

/** Stable output identity for generate/regenerate (no serial suffixes). */
struct GeneratedSfxOutputIdentity {
    AssetId assetId;
    std::string relativePath;
    std::filesystem::path finalPath;
    std::filesystem::path stagingPath;
};

/** Prefer the already-linked output when present; otherwise allocate the stable pair. */
[[nodiscard]] std::optional<GeneratedSfxOutputIdentity> stableGeneratedSfxOutputIdentity(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot);

} // namespace ArtCade::EditorNative
