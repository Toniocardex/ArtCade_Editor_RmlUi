#pragma once

#include "editor-native/app/export/export_types.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ArtCade::EditorNative {

struct RuntimeTemplateInfo {
    bool ok = false;
    std::filesystem::path directory;
    std::filesystem::path entryPointPath;
    std::string entryPoint;
    std::string engineVersion;
    std::string runtimeBuildId;
    std::string assetKeyId;
    int projectFormatMin = 0;
    int projectFormatMax = 0;
    bool supportsEncryptedArtcade = false;
    std::uintmax_t entryPointSize = 0;
    std::string entryPointSha256;
    std::vector<ExportDiagnostic> diagnostics;
};

class RuntimeTemplateCatalog {
public:
    explicit RuntimeTemplateCatalog(std::filesystem::path exportTemplatesRoot);

    RuntimeTemplateInfo resolve(ExportTarget target, int projectFormatVersion,
                                std::string_view packerAssetKeyId) const;

private:
    std::filesystem::path root_;
};

} // namespace ArtCade::EditorNative
