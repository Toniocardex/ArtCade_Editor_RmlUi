#pragma once

#include "editor-native/app/generated_sfx_output_transaction.h"
#include "editor-native/model/generated_sfx_policy.h"

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

// Application/filesystem guard for Generate. ProjectDocument remains the
// authoring authority; an existing unregistered file is an external collision
// that must block before a render job or authoring workflow starts.
enum class GeneratedSfxGenerationDisposition {
    Blocked,
    GenerateNew,
    ReplaceOwned,
};

enum class GeneratedSfxGenerationBlocker {
    None,
    ProjectUnsaved,
    Collision,
    Filesystem,
    InvalidIdentity,
};

struct GeneratedSfxGenerationPreflight {
    GeneratedSfxGenerationDisposition disposition =
        GeneratedSfxGenerationDisposition::Blocked;
    GeneratedSfxGenerationBlocker blocker = GeneratedSfxGenerationBlocker::None;
    GeneratedSfxOutputIdentity identity;
    std::string error;

    bool allowed() const {
        return disposition != GeneratedSfxGenerationDisposition::Blocked;
    }
    bool regenerating() const {
        return disposition == GeneratedSfxGenerationDisposition::ReplaceOwned;
    }

    static GeneratedSfxGenerationPreflight success(
        GeneratedSfxOutputIdentity outputIdentity,
        GeneratedSfxGenerationDisposition disposition);
    static GeneratedSfxGenerationPreflight failure(
        std::string message,
        GeneratedSfxGenerationBlocker blocker =
            GeneratedSfxGenerationBlocker::InvalidIdentity);
};

[[nodiscard]] GeneratedSfxGenerationPreflight preflightGeneratedSfxGeneration(
    const ProjectDocument& document,
    const artcade::sfx::GeneratedSfxDef& definition,
    const std::filesystem::path& projectRoot,
    const GeneratedSfxOutputRepository& repository);

} // namespace ArtCade::EditorNative
