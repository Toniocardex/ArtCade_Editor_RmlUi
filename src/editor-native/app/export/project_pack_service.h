#pragma once

#include "editor-native/app/export/export_types.h"

#include <filesystem>

namespace ArtCade::EditorNative {

struct ProjectPackServiceResult {
    bool ok = false;
    std::filesystem::path archivePath;
    std::uintmax_t archiveSize = 0;
    std::string sha256;
    std::vector<ExportDiagnostic> diagnostics;
};

class ProjectPackService {
public:
    // Builds an allowlist pack plan and writes ARTCADE1 game.artcade.
    ProjectPackServiceResult packAllowlist(
        const ExportProjectSnapshot& snapshot,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& outputArtcade) const;
};

} // namespace ArtCade::EditorNative
