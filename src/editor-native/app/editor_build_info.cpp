#include "editor-native/app/editor_build_info.h"

#include "editor-native/app/export/export_types.h"
#include "editor-native/app/export/runtime_template_catalog.h"
#include "editor-native/model/project_io.h"

namespace ArtCade::EditorNative {

EditorBuildInfo makeEditorBuildInfo() {
    EditorBuildInfo info;
    info.productName = "ArtCade Studio";
    info.editorVersion = "0.1.0";
    info.editorBuildId = ARTCADE_EDITOR_BUILD_ID;
    info.projectSchemaVersion = currentProjectSchemaVersion();
    info.copyright = "Copyright © ArtCade";
    return info;
}

BundledRuntimeInfo loadBundledRuntimeInfo(const std::filesystem::path& exportTemplatesRoot) {
    BundledRuntimeInfo out;
    if (exportTemplatesRoot.empty()) return out;

    RuntimeTemplateCatalog catalog(exportTemplatesRoot);
    const RuntimeTemplateInfo peek =
        catalog.peekManifest(ExportTarget::WindowsX64);
    if (!peek.ok) return out;

    out.available = true;
    out.engineVersion = peek.engineVersion;
    out.runtimeBuildId = peek.runtimeBuildId;
    out.platformerGroundSupport = peek.platformerGroundSupport;
    out.projectFormatMinimum = peek.projectFormatMin;
    out.projectFormatMaximum = peek.projectFormatMax;
    return out;
}

AboutArtCadeProjection buildAboutArtCadeProjection(
    const std::filesystem::path& exportTemplatesRoot) {
    AboutArtCadeProjection projection;
    projection.editor = makeEditorBuildInfo();
    projection.runtime = loadBundledRuntimeInfo(exportTemplatesRoot);
    return projection;
}

} // namespace ArtCade::EditorNative
