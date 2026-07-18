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

std::string trimCopy(std::string_view value) {
    std::size_t begin = 0;
    while (begin < value.size()
           && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    std::size_t end = value.size();
    while (end > begin
           && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return std::string(value.substr(begin, end - begin));
}

/** "Coin 02" / "Coin 2" → "Coin"; otherwise the trimmed name. */
std::string uniqueNameStem(std::string name) {
    name = trimCopy(name);
    if (name.empty()) return {};
    const auto space = name.rfind(' ');
    if (space == std::string::npos || space + 1 >= name.size()) return name;
    const std::string suffix = name.substr(space + 1);
    if (suffix.empty()
        || !std::all_of(suffix.begin(), suffix.end(),
                        [](unsigned char c) { return std::isdigit(c) != 0; })) {
        return name;
    }
    return trimCopy(name.substr(0, space));
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

std::string normalizePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

bool audioIsLinkedGeneratedOutput(const ProjectDoc& document, const AudioAssetDef& asset) {
    return std::any_of(document.generatedSfx.begin(), document.generatedSfx.end(),
        [&](const artcade::sfx::GeneratedSfxDef& definition) {
            return !definition.outputAssetId.empty()
                && definition.outputAssetId == asset.assetId;
        });
}

} // namespace

bool audioDisplayNameExists(
    const ProjectDoc& document,
    std::string_view candidate,
    const std::optional<std::string>& exceptSfxId,
    const std::optional<AssetId>& exceptAudioAssetId) {
    const std::string needle = folded(trimCopy(candidate));
    if (needle.empty()) return false;
    if (std::any_of(document.generatedSfx.begin(), document.generatedSfx.end(),
            [&](const artcade::sfx::GeneratedSfxDef& definition) {
                if (exceptSfxId && definition.id == *exceptSfxId) return false;
                return folded(trimCopy(definition.name)) == needle;
            })) {
        return true;
    }
    return std::any_of(document.audioAssets.begin(), document.audioAssets.end(),
        [&](const AudioAssetDef& asset) {
            if (exceptAudioAssetId && asset.assetId == *exceptAudioAssetId) return false;
            // Linked generated outputs share the recipe display name — already counted.
            if (audioIsLinkedGeneratedOutput(document, asset)) return false;
            if (asset.name.empty()) return false;
            return folded(trimCopy(asset.name)) == needle;
        });
}

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

AssetId generatedAudioAssetId(const std::string& generatedSfxId) {
    return "generated-audio-" + generatedSfxId;
}

std::string generatedAudioRelativePath(const std::string& generatedSfxId) {
    return "assets/audio/generated/" + generatedAudioAssetId(generatedSfxId) + ".wav";
}

CreateGeneratedSfxCommand::CreateGeneratedSfxCommand(
    std::string id, std::string name, artcade::sfx::SfxRecipe recipe)
    : id_(std::move(id)), name_(std::move(name)), recipe_(std::move(recipe)) {}

EditorOperationResult CreateGeneratedSfxCommand::apply(ProjectDocument& document) {
    if (!captured_) {
        const std::string name = trimCopy(name_);
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
        const std::string name = trimCopy(name_);
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
        before_ = document.data();
        after_ = before_;
        auto* definition = findDefinition(after_, id_);
        definition->name = name;
        // Keep the linked AudioAssetDef.name mirrored so the generated file
        // always carries the user-facing default until they rename again.
        if (!definition->outputAssetId.empty()) {
            if (AudioAssetDef* output =
                    findAudioAssetMutable(after_, definition->outputAssetId)) {
                output->name = name;
            }
        }
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
        const std::string newName = trimCopy(newName_);
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
            || normalizePath(outputAsset_.sourcePath) != normalizePath(canonicalPath)) {
            return EditorOperationResult::failure(
                "Generated SFX output must use the canonical asset id and path");
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
                || normalizePath(current->outputPath) != normalizePath(canonicalPath)) {
                return EditorOperationResult::failure(
                    "Generated SFX already owns a non-canonical output identity");
            }
            if (!document.hasAudioAsset(canonicalId)) {
                return EditorOperationResult::failure(
                    "Linked generated audio asset is missing: " + canonicalId);
            }
            const AudioAssetDef* existing = document.findAudioAsset(canonicalId);
            if (!existing
                || normalizePath(existing->sourcePath) != normalizePath(canonicalPath)) {
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

        before_ = document.data();
        after_ = before_;
        auto* definition = findDefinition(after_, id_);
        AudioAssetDef linkedOutput = outputAsset_;
        linkedOutput.assetId = canonicalId;
        linkedOutput.sourcePath = canonicalPath;
        // Stamp the recipe display name onto the AudioAssetDef (default on the
        // file). While linked, UI display still resolves from GeneratedSfxDef.name.
        linkedOutput.name = definition->name;
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

std::string nextGeneratedSfxId(const ProjectDocument& document) {
    int index = 1;
    std::string id;
    do {
        id = "generated-sfx-" + std::to_string(index++);
    } while (document.hasGeneratedSfx(id)
             || document.hasAudioAsset(generatedAudioAssetId(id)));
    return id;
}

std::string uniqueGeneratedSfxName(const ProjectDocument& document,
                                   const std::string& baseName) {
    std::string stem = uniqueNameStem(baseName);
    if (stem.empty()) stem = "Generated SFX";
    if (!audioDisplayNameExists(document.data(), stem)) return stem;
    for (int index = 2; index < 10000; ++index) {
        char suffix[8];
        std::snprintf(suffix, sizeof(suffix), " %02d", index);
        const std::string candidate = stem + suffix;
        if (!audioDisplayNameExists(document.data(), candidate)) return candidate;
    }
    return stem + " 9999";
}

bool generatedSfxOutputPathTaken(const ProjectDocument& document,
                                 const std::string& relativePath,
                                 const std::optional<AssetId>& exceptAssetId) {
    if (relativePath.empty()) return false;
    const std::string needle = normalizePath(relativePath);
    return std::any_of(document.data().audioAssets.begin(), document.data().audioAssets.end(),
        [&](const AudioAssetDef& asset) {
            if (exceptAssetId && asset.assetId == *exceptAssetId) return false;
            return normalizePath(asset.sourcePath) == needle;
        });
}

std::optional<GeneratedSfxOutputIdentity> stableGeneratedSfxOutputIdentity(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot) {
    const AssetId canonicalId = generatedAudioAssetId(definition.id);
    const std::string canonicalPath = generatedAudioRelativePath(definition.id);
    GeneratedSfxOutputIdentity identity;
    identity.assetId = canonicalId;
    identity.relativePath = canonicalPath;

    if (!definition.outputAssetId.empty()
        && document.hasAudioAsset(definition.outputAssetId)) {
        // Regenerate: only the canonical pair is allowed.
        if (definition.outputAssetId != canonicalId
            || normalizePath(definition.outputPath) != normalizePath(canonicalPath)) {
            return std::nullopt;
        }
    } else {
        // First generation: refuse collisions with foreign catalog entries.
        if (document.hasAudioAsset(canonicalId)) return std::nullopt;
        if (generatedSfxOutputPathTaken(document, canonicalPath)) return std::nullopt;
    }

    if (projectRoot.empty()) return identity;

    const auto confined = resolvePathInsideRoot(
        projectRoot, std::filesystem::u8path(identity.relativePath));
    if (!confined.ok) return std::nullopt;
    identity.finalPath = confined.value;
    identity.stagingPath = identity.finalPath.parent_path()
        / (identity.finalPath.stem().string() + ".artcade-pending.wav");
    return identity;
}

} // namespace ArtCade::EditorNative
