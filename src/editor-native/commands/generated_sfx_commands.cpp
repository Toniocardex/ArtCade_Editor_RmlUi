#include "editor-native/commands/generated_sfx_commands.h"

#include "artcade/sfx/recipe_json.hpp"
#include "artcade/sfx/synthesizer.hpp"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/project_document.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <utility>

namespace ArtCade::EditorNative {
namespace {

constexpr EditorInvalidation kInvalidation =
    EditorInvalidation::Assets | EditorInvalidation::Project;

std::string folded(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool nameExists(const ProjectDoc& document, const std::string& name,
                const std::string& exceptId = {}) {
    const std::string candidate = folded(name);
    return std::any_of(document.generatedSfx.begin(), document.generatedSfx.end(),
        [&](const artcade::sfx::GeneratedSfxDef& definition) {
            return definition.id != exceptId && folded(definition.name) == candidate;
        });
}

artcade::sfx::GeneratedSfxDef* findDefinition(ProjectDoc& document,
                                              const std::string& id) {
    const auto it = std::find_if(document.generatedSfx.begin(), document.generatedSfx.end(),
        [&](const artcade::sfx::GeneratedSfxDef& definition) { return definition.id == id; });
    return it == document.generatedSfx.end() ? nullptr : &*it;
}

AudioAssetDef* findAudioAssetMutable(ProjectDoc& document, const AssetId& id) {
    const auto it = std::find_if(document.audioAssets.begin(), document.audioAssets.end(),
        [&](const AudioAssetDef& asset) { return asset.assetId == id; });
    return it == document.audioAssets.end() ? nullptr : &*it;
}

/** Ownership handoff: recipe no longer owns the WAV → AudioAssetDef.name becomes
 *  the display authority. Not a sync of two live sources. */
void transferOutputNameOnDetach(ProjectDoc& document,
                                const artcade::sfx::GeneratedSfxDef& definition) {
    if (definition.outputAssetId.empty()) return;
    if (AudioAssetDef* output = findAudioAssetMutable(document, definition.outputAssetId))
        output->name = definition.name;
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

bool generatedSfxRecipesEqual(const artcade::sfx::SfxRecipe& left,
                              const artcade::sfx::SfxRecipe& right) {
    return artcade::sfx::recipeFingerprint(left) == artcade::sfx::recipeFingerprint(right);
}

GeneratedSfxOutputStatus generatedSfxOutputStatus(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition) {
    if (definition.outputAssetId.empty()
        || !document.hasAudioAsset(definition.outputAssetId)) {
        return GeneratedSfxOutputStatus::NeedsGeneration;
    }
    if (definition.generatedRecipeFingerprint
        == artcade::sfx::recipeFingerprint(definition.recipe)) {
        return GeneratedSfxOutputStatus::Ready;
    }
    return GeneratedSfxOutputStatus::Stale;
}

const char* generatedSfxOutputStatusLabel(GeneratedSfxOutputStatus status) {
    switch (status) {
    case GeneratedSfxOutputStatus::NeedsGeneration: return "Needs generation";
    case GeneratedSfxOutputStatus::Stale: return "Stale";
    case GeneratedSfxOutputStatus::Ready: return "Ready";
    }
    return "Needs generation";
}

CreateGeneratedSfxCommand::CreateGeneratedSfxCommand(
    std::string id, std::string name, artcade::sfx::SfxRecipe recipe)
    : id_(std::move(id)), name_(std::move(name)), recipe_(std::move(recipe)) {}

EditorOperationResult CreateGeneratedSfxCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        if (id_.empty() || name_.empty()) {
            return EditorOperationResult::failure("Generated SFX needs an id and a name");
        }
        if (document.hasGeneratedSfx(id_)) {
            return EditorOperationResult::failure("Generated SFX id already exists: " + id_);
        }
        if (document.hasAudioAsset("generated-audio-" + id_)) {
            return EditorOperationResult::failure(
                "Generated SFX id is still owned by an existing generated audio asset");
        }
        if (nameExists(document.data(), name_)) {
            return EditorOperationResult::failure("Generated SFX name already exists: " + name_);
        }
        const auto valid = validateRecipe(recipe_);
        if (!valid.ok) return valid;
        before_ = document.data();
        after_ = before_;
        artcade::sfx::GeneratedSfxDef definition;
        definition.id = id_;
        definition.name = name_;
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
        if (name_.empty()) return EditorOperationResult::failure("SFX name cannot be empty");
        if (!document.hasGeneratedSfx(id_)) {
            return EditorOperationResult::failure("Unknown Generated SFX: " + id_);
        }
        if (nameExists(document.data(), name_, id_)) {
            return EditorOperationResult::failure("Generated SFX name already exists: " + name_);
        }
        before_ = document.data();
        after_ = before_;
        findDefinition(after_, id_)->name = name_;
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
        if (const artcade::sfx::GeneratedSfxDef* definition = findDefinition(after_, id_))
            transferOutputNameOnDetach(after_, *definition);
        after_.generatedSfx.erase(std::remove_if(
            after_.generatedSfx.begin(), after_.generatedSfx.end(),
            [&](const artcade::sfx::GeneratedSfxDef& definition) {
                return definition.id == id_;
            }), after_.generatedSfx.end());
        // Generated output is deliberately retained as a normal AudioAssetDef.
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return success(id_);
}

EditorOperationResult RemoveGeneratedSfxCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX removal");
    document.commitStagedCommand(before_);
    return success(id_);
}

DuplicateGeneratedSfxCommand::DuplicateGeneratedSfxCommand(
    std::string sourceId, std::string newId, std::string newName)
    : sourceId_(std::move(sourceId)), newId_(std::move(newId)), newName_(std::move(newName)) {}

EditorOperationResult DuplicateGeneratedSfxCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const auto* source = document.findGeneratedSfx(sourceId_);
        if (!source) return EditorOperationResult::failure("Unknown Generated SFX: " + sourceId_);
        if (newId_.empty() || newName_.empty()) {
            return EditorOperationResult::failure("Generated SFX needs an id and a name");
        }
        if (document.hasGeneratedSfx(newId_)) {
            return EditorOperationResult::failure("Generated SFX id already exists: " + newId_);
        }
        if (document.hasAudioAsset("generated-audio-" + newId_)) {
            return EditorOperationResult::failure(
                "Generated SFX id is still owned by an existing generated audio asset");
        }
        if (nameExists(document.data(), newName_)) {
            return EditorOperationResult::failure("Generated SFX name already exists: " + newName_);
        }
        const auto valid = validateRecipe(source->recipe);
        if (!valid.ok) return valid;
        before_ = document.data();
        after_ = before_;
        artcade::sfx::GeneratedSfxDef clone;
        clone.id = newId_;
        clone.name = newName_;
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
        if (outputAsset_.assetId.empty() || outputAsset_.sourcePath.empty()) {
            return EditorOperationResult::failure("Generated SFX output asset is incomplete");
        }
        std::string pathError;
        if (!isSafeProjectRelativePath(std::filesystem::u8path(outputAsset_.sourcePath),
                                       &pathError)) {
            return EditorOperationResult::failure("Unsafe generated output path: " + pathError);
        }
        before_ = document.data();
        after_ = before_;
        auto* definition = findDefinition(after_, id_);
        AudioAssetDef linkedOutput = outputAsset_;
        // Linked output has no parallel name authority — display comes from
        // GeneratedSfxDef.name until the recipe detaches.
        linkedOutput.name.clear();
        // Regenerating into a new asset keeps the previous WAV as an independent
        // AudioAssetDef (name handoff). Same-id updates stay in place.
        if (!definition->outputAssetId.empty()
            && definition->outputAssetId != linkedOutput.assetId) {
            transferOutputNameOnDetach(after_, *definition);
        }
        auto audio = std::find_if(after_.audioAssets.begin(), after_.audioAssets.end(),
            [&](const AudioAssetDef& asset) { return asset.assetId == linkedOutput.assetId; });
        if (audio == after_.audioAssets.end()) {
            after_.audioAssets.push_back(linkedOutput);
        } else if (audio->sourcePath == linkedOutput.sourcePath) {
            *audio = linkedOutput;
        } else {
            return EditorOperationResult::failure(
                "Generated SFX output asset id conflicts with an existing asset");
        }
        definition->outputAssetId = linkedOutput.assetId;
        definition->outputPath = linkedOutput.sourcePath;
        definition->generatedRecipeFingerprint =
            artcade::sfx::recipeFingerprint(expectedRecipe_);
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

CreateGeneratedSfxOutputCommand::CreateGeneratedSfxOutputCommand(
    std::string id, artcade::sfx::SfxRecipe expectedRecipe, AudioAssetDef outputAsset)
    : id_(std::move(id)), expectedRecipe_(std::move(expectedRecipe)),
      outputAsset_(std::move(outputAsset)) {}

EditorOperationResult CreateGeneratedSfxOutputCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const auto* current = document.findGeneratedSfx(id_);
        if (!current) return EditorOperationResult::failure("Unknown Generated SFX: " + id_);
        if (!generatedSfxRecipesEqual(current->recipe, expectedRecipe_)) {
            return EditorOperationResult::failure(
                "Generated SFX recipe changed while rendering; stale output discarded");
        }
        if (outputAsset_.assetId.empty() || outputAsset_.sourcePath.empty()) {
            return EditorOperationResult::failure("Generated SFX output asset is incomplete");
        }
        if (document.hasAudioAsset(outputAsset_.assetId)) {
            return EditorOperationResult::failure(
                "Generated audio asset already exists: " + outputAsset_.assetId);
        }
        if (generatedSfxOutputPathTaken(document, outputAsset_.sourcePath)) {
            return EditorOperationResult::failure(
                "Generated audio output path already exists: " + outputAsset_.sourcePath);
        }
        std::string pathError;
        if (!isSafeProjectRelativePath(std::filesystem::u8path(outputAsset_.sourcePath),
                                       &pathError)) {
            return EditorOperationResult::failure("Unsafe generated output path: " + pathError);
        }
        before_ = document.data();
        after_ = before_;
        auto* definition = findDefinition(after_, id_);
        if (!definition->outputAssetId.empty())
            transferOutputNameOnDetach(after_, *definition);
        AudioAssetDef linkedOutput = outputAsset_;
        linkedOutput.name.clear();
        after_.audioAssets.push_back(linkedOutput);
        definition->outputAssetId = linkedOutput.assetId;
        definition->outputPath = linkedOutput.sourcePath;
        definition->generatedRecipeFingerprint =
            artcade::sfx::recipeFingerprint(expectedRecipe_);
        captured_ = true;
    }
    document.commitStagedCommand(after_);
    return success(id_);
}

EditorOperationResult CreateGeneratedSfxOutputCommand::undo(ProjectDocument& document) {
    if (!captured_) return EditorOperationResult::failure("Cannot undo SFX output creation");
    document.commitStagedCommand(before_);
    return success(id_);
}

std::string nextGeneratedSfxId(const ProjectDocument& document) {
    int index = 1;
    std::string id;
    do {
        id = "generated-sfx-" + std::to_string(index++);
    } while (document.hasGeneratedSfx(id)
             || document.hasAudioAsset("generated-audio-" + id));
    return id;
}

std::string uniqueGeneratedSfxName(const ProjectDocument& document,
                                   const std::string& baseName) {
    const std::string base = baseName.empty() ? "Generated SFX" : baseName;
    if (!nameExists(document.data(), base)) return base;
    for (int index = 1; index < 10000; ++index) {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), " %02d", index);
        const std::string candidate = base + suffix;
        if (!nameExists(document.data(), candidate)) return candidate;
    }
    return base + " copy";
}

bool generatedSfxOutputPathTaken(const ProjectDocument& document,
                                 const std::string& relativePath) {
    if (relativePath.empty()) return false;
    auto normalize = [](std::string path) {
        std::replace(path.begin(), path.end(), '\\', '/');
        return path;
    };
    const std::string needle = normalize(relativePath);
    return std::any_of(document.data().audioAssets.begin(), document.data().audioAssets.end(),
        [&](const AudioAssetDef& asset) {
            return normalize(asset.sourcePath) == needle;
        });
}

std::optional<GeneratedSfxOutputIdentity> nextGeneratedSfxOutputIdentity(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot) {
    for (std::uint32_t serial = 1; serial < 100000u; ++serial) {
        char suffix[16];
        std::snprintf(suffix, sizeof(suffix), "%04u", serial);
        GeneratedSfxOutputIdentity identity;
        identity.assetId = "generated-audio-" + definition.id + "-" + suffix;
        identity.relativePath = "assets/audio/generated/" + identity.assetId + ".wav";
        if (document.hasAudioAsset(identity.assetId)) continue;
        if (generatedSfxOutputPathTaken(document, identity.relativePath)) continue;

        if (projectRoot.empty()) {
            // Catalog-only allocation (unit tests without a saved project root).
            return identity;
        }

        const auto confined = resolvePathInsideRoot(
            projectRoot, std::filesystem::u8path(identity.relativePath));
        if (!confined.ok) continue;
        std::error_code existsError;
        if (std::filesystem::exists(confined.value, existsError)) continue;

        identity.finalPath = confined.value;
        identity.stagingPath = identity.finalPath.parent_path()
            / (identity.finalPath.stem().string() + ".artcade-pending.wav");
        return identity;
    }
    return std::nullopt;
}

} // namespace ArtCade::EditorNative
