#pragma once

#include "editor-native/commands/generated_sfx_commands.h"

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

// Application/filesystem guard for Generate. ProjectDocument remains the
// authoring authority; an existing unregistered file is an external collision
// that must block before a render job or authoring workflow starts.
struct GeneratedSfxGenerationPreflight {
    bool allowed = false;
    bool regenerating = false;
    GeneratedSfxOutputIdentity identity;
    std::string error;

    static GeneratedSfxGenerationPreflight success(
        GeneratedSfxOutputIdentity outputIdentity, bool isRegenerating);
    static GeneratedSfxGenerationPreflight failure(std::string message);
};

[[nodiscard]] GeneratedSfxGenerationPreflight preflightGeneratedSfxGeneration(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot);

} // namespace ArtCade::EditorNative
