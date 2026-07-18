#include "editor-native/app/generated_sfx_status_projection.h"

#include "editor-native/model/generated_sfx_policy.h"
#include "artcade/sfx/recipe_json.hpp"

namespace ArtCade::EditorNative {

GeneratedSfxStatusProjection projectGeneratedSfxStatus(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot,
    const GeneratedSfxOutputRepository& repository,
    const GeneratedSfxGenerationSnapshot& service) {
    const std::string fingerprint = artcade::sfx::recipeFingerprint(definition.recipe);
    if (service.generating && service.generating->assetId == definition.id
        && service.generating->recipeFingerprint == fingerprint) {
        return {GeneratedSfxObservedStatus::Generating, "Generating audio asset"};
    }

    const auto diagnostic = service.diagnostics.find(definition.id);
    if (diagnostic != service.diagnostics.end()
        && diagnostic->second.recipeFingerprint == fingerprint) {
        return {
            diagnostic->second.kind == GeneratedSfxGenerationFailureKind::Collision
                ? GeneratedSfxObservedStatus::Collision
                : GeneratedSfxObservedStatus::GenerationFailed,
            diagnostic->second.message};
    }

    const GeneratedSfxOutputStatus authored =
        generatedSfxOutputStatus(document, definition);
    const auto identity = stableGeneratedSfxOutputIdentity(
        document, definition, projectRoot);
    if (!identity) {
        return {GeneratedSfxObservedStatus::Collision,
                "Generated SFX output id or path is already owned"};
    }
    if (projectRoot.empty()) {
        return {GeneratedSfxObservedStatus::MissingOutput,
                "Save the project before generating an audio asset"};
    }

    const GeneratedSfxFileInspection file = repository.inspect(identity->finalPath);
    if (!file.ok) {
        return {GeneratedSfxObservedStatus::MissingOutput,
                "Could not inspect audio output: " + file.error};
    }
    if (authored == GeneratedSfxOutputStatus::NeedsGeneration && file.exists) {
        return {GeneratedSfxObservedStatus::Collision,
                "Audio file already exists: " + identity->relativePath};
    }
    if (!file.exists || !file.readable) {
        return {GeneratedSfxObservedStatus::MissingOutput,
                !file.exists ? "Audio output is missing"
                             : "Audio output is not readable"};
    }
    if (authored == GeneratedSfxOutputStatus::Stale) {
        return {GeneratedSfxObservedStatus::RecipeModified,
                "Recipe modified; regenerate audio asset"};
    }
    if (authored == GeneratedSfxOutputStatus::Ready) {
        return {GeneratedSfxObservedStatus::UpToDate, "Audio asset ready"};
    }
    return {GeneratedSfxObservedStatus::Collision,
            "Unregistered audio output already exists"};
}

const char* generatedSfxObservedStatusLabel(GeneratedSfxObservedStatus status) {
    switch (status) {
    case GeneratedSfxObservedStatus::UpToDate: return "Up to date";
    case GeneratedSfxObservedStatus::RecipeModified: return "Recipe modified";
    case GeneratedSfxObservedStatus::MissingOutput: return "Missing output";
    case GeneratedSfxObservedStatus::Generating: return "Generating";
    case GeneratedSfxObservedStatus::GenerationFailed: return "Generation failed";
    case GeneratedSfxObservedStatus::Collision: return "Collision";
    }
    return "Missing output";
}

} // namespace ArtCade::EditorNative
