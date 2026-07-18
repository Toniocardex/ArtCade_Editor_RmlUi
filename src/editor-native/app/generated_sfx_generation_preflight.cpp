#include "editor-native/app/generated_sfx_generation_preflight.h"
#include "editor-native/model/project_document.h"

#include <system_error>
#include <utility>

namespace ArtCade::EditorNative {

GeneratedSfxGenerationPreflight GeneratedSfxGenerationPreflight::success(
    GeneratedSfxOutputIdentity outputIdentity, bool isRegenerating) {
    GeneratedSfxGenerationPreflight result;
    result.allowed = true;
    result.regenerating = isRegenerating;
    result.identity = std::move(outputIdentity);
    return result;
}

GeneratedSfxGenerationPreflight GeneratedSfxGenerationPreflight::failure(
    std::string message) {
    GeneratedSfxGenerationPreflight result;
    result.error = std::move(message);
    return result;
}

GeneratedSfxGenerationPreflight preflightGeneratedSfxGeneration(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot) {
    if (definition.id.empty())
        return GeneratedSfxGenerationPreflight::failure(
            "Generated SFX needs an id before audio can be created");
    if (projectRoot.empty())
        return GeneratedSfxGenerationPreflight::failure(
            "Save the project before generating a WAV asset");

    const bool regenerating = !definition.outputAssetId.empty()
        && document.hasAudioAsset(definition.outputAssetId);
    auto identity = stableGeneratedSfxOutputIdentity(document, definition, projectRoot);
    if (!identity)
        return GeneratedSfxGenerationPreflight::failure(
            "Generated SFX output id or path is already owned by another asset");
    if (identity->finalPath.empty())
        return GeneratedSfxGenerationPreflight::failure(
            "Could not resolve the Generated SFX output path");

    std::error_code existsError;
    const bool destinationExists = std::filesystem::exists(identity->finalPath, existsError);
    if (existsError)
        return GeneratedSfxGenerationPreflight::failure(
            "Could not inspect Generated SFX destination: " + existsError.message());
    if (!regenerating && destinationExists)
        return GeneratedSfxGenerationPreflight::failure(
            "Audio file already exists: " + identity->relativePath);

    return GeneratedSfxGenerationPreflight::success(std::move(*identity), regenerating);
}

} // namespace ArtCade::EditorNative
