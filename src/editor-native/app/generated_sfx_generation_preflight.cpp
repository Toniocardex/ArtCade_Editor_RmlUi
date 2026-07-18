#include "editor-native/app/generated_sfx_generation_preflight.h"
#include "editor-native/model/project_document.h"

#include <system_error>
#include <utility>

namespace ArtCade::EditorNative {

GeneratedSfxGenerationPreflight GeneratedSfxGenerationPreflight::success(
    GeneratedSfxOutputIdentity outputIdentity,
    GeneratedSfxGenerationDisposition disposition) {
    GeneratedSfxGenerationPreflight result;
    result.disposition = disposition;
    result.identity = std::move(outputIdentity);
    return result;
}

GeneratedSfxGenerationPreflight GeneratedSfxGenerationPreflight::failure(
    std::string message, GeneratedSfxGenerationBlocker blocker) {
    GeneratedSfxGenerationPreflight result;
    result.error = std::move(message);
    result.blocker = blocker;
    return result;
}

GeneratedSfxGenerationPreflight preflightGeneratedSfxGeneration(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot,
    const GeneratedSfxOutputRepository& repository) {
    if (definition.id.empty())
        return GeneratedSfxGenerationPreflight::failure(
            "Generated SFX needs an id before audio can be created");
    if (projectRoot.empty())
        return GeneratedSfxGenerationPreflight::failure(
            "Save the project before generating a WAV asset",
            GeneratedSfxGenerationBlocker::ProjectUnsaved);

    const bool regenerating = !definition.outputAssetId.empty()
        && document.hasAudioAsset(definition.outputAssetId);
    auto identity = stableGeneratedSfxOutputIdentity(document, definition, projectRoot);
    if (!identity)
        return GeneratedSfxGenerationPreflight::failure(
            "Generated SFX output id or path is already owned by another asset",
            GeneratedSfxGenerationBlocker::Collision);
    if (identity->finalPath.empty())
        return GeneratedSfxGenerationPreflight::failure(
            "Could not resolve the Generated SFX output path");

    const GeneratedSfxFileInspection destination =
        repository.inspect(identity->finalPath);
    if (!destination.ok)
        return GeneratedSfxGenerationPreflight::failure(
            "Could not inspect Generated SFX destination: " + destination.error,
            GeneratedSfxGenerationBlocker::Filesystem);
    if (!regenerating && destination.exists)
        return GeneratedSfxGenerationPreflight::failure(
            "Audio file already exists: " + identity->relativePath,
            GeneratedSfxGenerationBlocker::Collision);

    return GeneratedSfxGenerationPreflight::success(
        std::move(*identity), regenerating
            ? GeneratedSfxGenerationDisposition::ReplaceOwned
            : GeneratedSfxGenerationDisposition::GenerateNew);
}

} // namespace ArtCade::EditorNative
