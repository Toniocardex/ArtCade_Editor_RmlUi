#pragma once

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

#ifndef ARTCADE_EDITOR_BUILD_ID
#define ARTCADE_EDITOR_BUILD_ID "local"
#endif

struct EditorBuildInfo {
    std::string productName;
    std::string editorVersion;
    std::string editorBuildId;
    int projectSchemaVersion = 0;
    std::string copyright;
};

struct BundledRuntimeInfo {
    bool available = false;
    std::string engineVersion;
    std::string runtimeBuildId;
    std::string platformerGroundSupport;
    int projectFormatMinimum = 0;
    int projectFormatMaximum = 0;
};

struct AboutArtCadeProjection {
    EditorBuildInfo editor;
    BundledRuntimeInfo runtime;
};

EditorBuildInfo makeEditorBuildInfo();
BundledRuntimeInfo loadBundledRuntimeInfo(const std::filesystem::path& exportTemplatesRoot);
AboutArtCadeProjection buildAboutArtCadeProjection(
    const std::filesystem::path& exportTemplatesRoot);

} // namespace ArtCade::EditorNative
