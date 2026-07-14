#include "editor-native/commands/generated_sfx_commands.h"

#include "artcade/sfx/recipe_json.hpp"
#include "artcade/sfx/synthesizer.hpp"
#include "editor-native/model/path_confinement.h"
#include "editor-native/model/project_document.h"

#include <algorithm>
#include <cctype>
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
    artcade::sfx::GeneratedSfxDef leftDef;
    leftDef.recipe = left;
    artcade::sfx::GeneratedSfxDef rightDef;
    rightDef.recipe = right;
    const auto leftJson = artcade::sfx::serializeRecipeJson(leftDef, -1);
    const auto rightJson = artcade::sfx::serializeRecipeJson(rightDef, -1);
    return leftJson.ok() && rightJson.ok() && leftJson.value() == rightJson.value();
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
        definition->recipe = recipe_;
        // A changed recipe must never masquerade as the previous generated
        // output. The old AudioAssetDef remains a normal catalog asset.
        definition->outputAssetId.clear();
        definition->outputPath.clear();
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
        auto audio = std::find_if(after_.audioAssets.begin(), after_.audioAssets.end(),
            [&](const AudioAssetDef& asset) { return asset.assetId == outputAsset_.assetId; });
        if (audio == after_.audioAssets.end()) {
            after_.audioAssets.push_back(outputAsset_);
        } else if (audio->sourcePath == outputAsset_.sourcePath) {
            *audio = outputAsset_;
        } else {
            return EditorOperationResult::failure(
                "Generated SFX output asset id conflicts with an existing asset");
        }
        definition->outputAssetId = outputAsset_.assetId;
        definition->outputPath = outputAsset_.sourcePath;
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
