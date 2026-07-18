#include "editor-native/commands/generated_sfx_commands.h"

#include "artcade/sfx/recipe_json.hpp"
#include "artcade/sfx/synthesizer.hpp"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/project_document.h"

#include <algorithm>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Project;
constexpr EditorInvalidation kDeleteInvalidation =
    kInvalidation | EditorInvalidation::LogicBoard;

artcade::sfx::GeneratedSfxDef* findDefinition(ProjectDoc& document,
                                              const std::string& id) {
    const auto it = std::find_if(document.generatedSfx.begin(), document.generatedSfx.end(),
        [&](const artcade::sfx::GeneratedSfxDef& definition) { return definition.id == id; });
    return it == document.generatedSfx.end() ? nullptr : &*it;
}

void clearAudioAssetReferences(ProjectDoc& document, const AssetId& assetId) {
    if (assetId.empty()) return;
    const auto clearBlock = [&](LogicBlockDef& block) {
        for (LogicPropertyDef& property : block.properties) {
            if (property.key != "audioAssetId") continue;
            auto* reference = std::get_if<LogicAssetReference>(&property.value);
            if (reference && reference->id == assetId)
                property.value = LogicAssetReference{};
        }
    };
    for (auto& [unused, objectType] : document.objectTypes) {
        (void)unused;
        if (!objectType.logicBoard) continue;
        for (LogicRuleDef& rule : objectType.logicBoard->rules) {
            clearBlock(rule.trigger);
            for (LogicConditionClause& condition : rule.conditions)
                clearBlock(condition.block);
            for (LogicBlockDef& action : rule.actions) clearBlock(action);
        }
    }
}

void clearGeneratedSfxProvenance(ProjectDoc& document,
                                 const std::string& generatedSfxId) {
    for (AudioAssetDef& audio : document.audioAssets) {
        if (audio.generatedFromSfxId
            && *audio.generatedFromSfxId == generatedSfxId) {
            audio.generatedFromSfxId.reset();
        }
    }
}

EditorOperationResult validateRecipe(const artcade::sfx::SfxRecipe& recipe) {
    const auto result = artcade::sfx::SfxSynthesizer::validate(recipe);
    return result.ok() ? EditorOperationResult::success(EditorInvalidation::None)
                       : EditorOperationResult::failure(result.error().message);
}

EditorOperationResult success(const std::string& id) {
    return EditorOperationResult::success(kInvalidation, DomainChange::assetChanged(id));
}

} // namespace

CreateGeneratedSfxCommand::CreateGeneratedSfxCommand(
    std::string id, std::string name, artcade::sfx::SfxRecipe recipe)
    : id_(std::move(id)), name_(std::move(name)), recipe_(std::move(recipe)) {}

EditorOperationResult CreateGeneratedSfxCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const std::string name = normalizeAudioDisplayName(name_);
        if (id_.empty() || name.empty()) {
            return EditorOperationResult::failure("Generated SFX needs an id and a name");
        }
        if (document.hasGeneratedSfx(id_)) {
            return EditorOperationResult::failure("Generated SFX id already exists: " + id_);
        }
        if (document.hasAudioAsset(generatedAudioAssetId(id_))) {
            return EditorOperationResult::failure(
                "Generated SFX id is still owned by an existing generated audio asset");
        }
        if (audioDisplayNameExists(document.data(), name)) {
            return EditorOperationResult::failure("Audio name already exists: " + name);
        }
        const auto valid = validateRecipe(recipe_);
        if (!valid.ok) return valid;
        before_ = document.data();
        after_ = before_;
        artcade::sfx::GeneratedSfxDef definition;
        definition.id = id_;
        definition.name = name;
        definition.recipe = recipe_;
        after_.generatedSfx.push_back(std::move(definition));
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return success(id_);
}

EditorOperationResult CreateGeneratedSfxCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX creation");
    document.commitStagedCommand(before_);
    return success(id_);
}

RenameGeneratedSfxCommand::RenameGeneratedSfxCommand(std::string id, std::string name)
    : id_(std::move(id)), name_(std::move(name)) {}

EditorOperationResult RenameGeneratedSfxCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const std::string name = normalizeAudioDisplayName(name_);
        if (name.empty()) return EditorOperationResult::failure("SFX name cannot be empty");
        if (!document.hasGeneratedSfx(id_)) {
            return EditorOperationResult::failure("Unknown Generated SFX: " + id_);
        }
        const auto* current = document.findGeneratedSfx(id_);
        const std::optional<AssetId> exceptAudio =
            current && !current->outputAssetId.empty()
                ? std::optional<AssetId>{current->outputAssetId}
                : std::nullopt;
        if (audioDisplayNameExists(document.data(), name, id_, exceptAudio)) {
            return EditorOperationResult::failure("Audio name already exists: " + name);
        }
        if (current->name == name)
            return EditorOperationResult::success(EditorInvalidation::None);
        before_ = document.data();
        after_ = before_;
        auto* definition = findDefinition(after_, id_);
        definition->name = name;
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return success(id_);
}

EditorOperationResult RenameGeneratedSfxCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX rename");
    document.commitStagedCommand(before_);
    return success(id_);
}

UpdateGeneratedSfxRecipeCommand::UpdateGeneratedSfxRecipeCommand(
    std::string id, artcade::sfx::SfxRecipe recipe)
    : id_(std::move(id)), recipe_(std::move(recipe)) {}

EditorOperationResult UpdateGeneratedSfxRecipeCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const auto valid = validateRecipe(recipe_);
        if (!valid.ok) return valid;
        const auto* current = document.findGeneratedSfx(id_);
        if (!current) return EditorOperationResult::failure("Unknown Generated SFX: " + id_);
        if (generatedSfxRecipesEqual(current->recipe, recipe_))
            return EditorOperationResult::success(EditorInvalidation::None);
        before_ = document.data();
        after_ = before_;
        auto* definition = findDefinition(after_, id_);
        // Keep the stable output link. Status becomes Stale when the current
        // recipe fingerprint no longer matches generatedRecipeFingerprint.
        definition->recipe = recipe_;
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return success(id_);
}

EditorOperationResult UpdateGeneratedSfxRecipeCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX recipe update");
    document.commitStagedCommand(before_);
    return success(id_);
}

RemoveGeneratedSfxCommand::RemoveGeneratedSfxCommand(std::string id)
    : id_(std::move(id)) {}

EditorOperationResult RemoveGeneratedSfxCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        if (!document.hasGeneratedSfx(id_)) {
            return EditorOperationResult::failure("Unknown Generated SFX: " + id_);
        }
        before_ = document.data();
        after_ = before_;
        const artcade::sfx::GeneratedSfxDef* definition = findDefinition(after_, id_);
        const AssetId outputAssetId = definition ? definition->outputAssetId : AssetId{};
        after_.generatedSfx.erase(std::remove_if(
            after_.generatedSfx.begin(), after_.generatedSfx.end(),
            [&](const artcade::sfx::GeneratedSfxDef& definition) {
                return definition.id == id_;
            }), after_.generatedSfx.end());
        if (!outputAssetId.empty()) {
            after_.audioAssets.erase(std::remove_if(
                after_.audioAssets.begin(), after_.audioAssets.end(),
                [&](const AudioAssetDef& asset) {
                    return asset.assetId == outputAssetId;
                }), after_.audioAssets.end());
            clearAudioAssetReferences(after_, outputAssetId);
        }
        clearGeneratedSfxProvenance(after_, id_);
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return EditorOperationResult::success(
        kDeleteInvalidation, DomainChange::projectChanged());
}

EditorOperationResult RemoveGeneratedSfxCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX removal");
    document.commitStagedCommand(before_);
    return EditorOperationResult::success(
        kDeleteInvalidation, DomainChange::projectChanged());
}

DuplicateGeneratedSfxCommand::DuplicateGeneratedSfxCommand(
    std::string sourceId, std::string newId, std::string newName)
    : sourceId_(std::move(sourceId)), newId_(std::move(newId)), newName_(std::move(newName)) {}

EditorOperationResult DuplicateGeneratedSfxCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const auto* source = document.findGeneratedSfx(sourceId_);
        if (!source) return EditorOperationResult::failure("Unknown Generated SFX: " + sourceId_);
        const std::string newName = normalizeAudioDisplayName(newName_);
        if (newId_.empty() || newName.empty()) {
            return EditorOperationResult::failure("Generated SFX needs an id and a name");
        }
        if (document.hasGeneratedSfx(newId_)) {
            return EditorOperationResult::failure("Generated SFX id already exists: " + newId_);
        }
        if (document.hasAudioAsset(generatedAudioAssetId(newId_))) {
            return EditorOperationResult::failure(
                "Generated SFX id is still owned by an existing generated audio asset");
        }
        if (audioDisplayNameExists(document.data(), newName)) {
            return EditorOperationResult::failure("Audio name already exists: " + newName);
        }
        const auto valid = validateRecipe(source->recipe);
        if (!valid.ok) return valid;
        before_ = document.data();
        after_ = before_;
        artcade::sfx::GeneratedSfxDef clone;
        clone.id = newId_;
        clone.name = newName;
        clone.recipe = source->recipe;
        // New identity: never share the source WAV / catalog link / fingerprint.
        clone.outputAssetId.clear();
        clone.outputPath.clear();
        clone.generatedRecipeFingerprint.clear();
        after_.generatedSfx.push_back(std::move(clone));
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return success(newId_);
}

EditorOperationResult DuplicateGeneratedSfxCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX duplicate");
    document.commitStagedCommand(before_);
    return success(newId_);
}

RegisterGeneratedSfxOutputCommand::RegisterGeneratedSfxOutputCommand(
    std::string id, artcade::sfx::SfxRecipe expectedRecipe, AudioAssetDef outputAsset)
    : id_(std::move(id)), expectedRecipe_(std::move(expectedRecipe)),
      outputAsset_(std::move(outputAsset)) {}

EditorOperationResult RegisterGeneratedSfxOutputCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const auto* current = document.findGeneratedSfx(id_);
        if (!current) return EditorOperationResult::failure("Unknown Generated SFX: " + id_);
        if (!generatedSfxRecipesEqual(current->recipe, expectedRecipe_)) {
            return EditorOperationResult::failure(
                "Generated SFX recipe changed while rendering; stale output discarded");
        }
        const AssetId canonicalId = generatedAudioAssetId(id_);
        const std::string canonicalPath = generatedAudioRelativePath(id_);
        if (outputAsset_.assetId != canonicalId
            || outputAsset_.sourcePath != canonicalPath) {
            return EditorOperationResult::failure(
                "Generated SFX output must use the canonical asset id and path");
        }
        if (outputAsset_.loadMode != AudioLoadMode::StaticSound) {
            return EditorOperationResult::failure(
                "Generated SFX output must use static audio load mode");
        }
        std::string pathError;
        if (!isSafeProjectRelativePath(std::filesystem::u8path(canonicalPath), &pathError)) {
            return EditorOperationResult::failure("Unsafe generated output path: " + pathError);
        }

        const bool firstGeneration = current->outputAssetId.empty();
        if (firstGeneration) {
            if (document.hasAudioAsset(canonicalId)) {
                return EditorOperationResult::failure(
                    "Canonical generated audio asset already exists: " + canonicalId);
            }
            if (generatedSfxOutputPathTaken(document, canonicalPath)) {
                return EditorOperationResult::failure(
                    "Generated audio output path already exists: " + canonicalPath);
            }
        } else {
            if (current->outputAssetId != canonicalId
                || current->outputPath != canonicalPath) {
                return EditorOperationResult::failure(
                    "Generated SFX already owns a non-canonical output identity");
            }
            if (!document.hasAudioAsset(canonicalId)) {
                return EditorOperationResult::failure(
                    "Linked generated audio asset is missing: " + canonicalId);
            }
            const AudioAssetDef* existing = document.findAudioAsset(canonicalId);
            if (!existing
                || existing->sourcePath != canonicalPath) {
                return EditorOperationResult::failure(
                    "Linked generated audio asset path mismatch");
            }
            if (existing->generatedFromSfxId
                && !existing->generatedFromSfxId->empty()
                && *existing->generatedFromSfxId != id_) {
                return EditorOperationResult::failure(
                    "Generated audio asset belongs to a different sound");
            }
        }

        const AudioAssetDef* existingOutput = document.findAudioAsset(canonicalId);
        const std::string expectedFingerprint =
            artcade::sfx::recipeFingerprint(expectedRecipe_);
        if (!firstGeneration && existingOutput
            && current->outputAssetId == canonicalId
            && current->outputPath == canonicalPath
            && current->generatedRecipeFingerprint == expectedFingerprint
            && existingOutput->sourcePath == canonicalPath
            && existingOutput->loadMode == AudioLoadMode::StaticSound
            && existingOutput->name.empty()
            && existingOutput->generatedFromSfxId
            && *existingOutput->generatedFromSfxId == id_) {
            return EditorOperationResult::success(EditorInvalidation::None);
        }

        before_ = document.data();
        after_ = before_;
        auto* definition = findDefinition(after_, id_);
        AudioAssetDef linkedOutput = outputAsset_;
        linkedOutput.assetId = canonicalId;
        linkedOutput.sourcePath = canonicalPath;
        // GeneratedSfxDef is the sole display-name authority while linked.
        linkedOutput.name.clear();
        linkedOutput.generatedFromSfxId = id_;
        auto audio = std::find_if(after_.audioAssets.begin(), after_.audioAssets.end(),
            [&](const AudioAssetDef& asset) { return asset.assetId == canonicalId; });
        if (audio == after_.audioAssets.end()) {
            after_.audioAssets.push_back(linkedOutput);
        } else {
            *audio = linkedOutput;
        }
        definition->outputAssetId = canonicalId;
        definition->outputPath = canonicalPath;
        definition->generatedRecipeFingerprint = expectedFingerprint;
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return success(id_);
}

EditorOperationResult RegisterGeneratedSfxOutputCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX output registration");
    document.commitStagedCommand(before_);
    return success(id_);
}

} // namespace ArtCade::EditorNative
