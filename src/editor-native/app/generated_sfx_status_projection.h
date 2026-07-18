#pragma once

#include "editor-native/app/generated_sfx_generation_service.h"
#include "editor-native/app/generated_sfx_output_transaction.h"

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

enum class GeneratedSfxObservedStatus {
    UpToDate,
    RecipeModified,
    MissingOutput,
    Generating,
    GenerationFailed,
    Collision,
};

struct GeneratedSfxStatusProjection {
    GeneratedSfxObservedStatus status = GeneratedSfxObservedStatus::MissingOutput;
    std::string message;

    bool canRetry() const {
        return status == GeneratedSfxObservedStatus::MissingOutput
            || status == GeneratedSfxObservedStatus::RecipeModified
            || status == GeneratedSfxObservedStatus::GenerationFailed
            || status == GeneratedSfxObservedStatus::Collision;
    }
};

[[nodiscard]] GeneratedSfxStatusProjection projectGeneratedSfxStatus(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot,
    const GeneratedSfxOutputRepository& repository,
    const GeneratedSfxGenerationSnapshot& service);

[[nodiscard]] const char* generatedSfxObservedStatusLabel(
    GeneratedSfxObservedStatus status);

} // namespace ArtCade::EditorNative
