#include "editor-native/app/export/runtime_template_catalog.h"

#include "artcade-archive/archive_util.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <string_view>

namespace ArtCade::EditorNative {

RuntimeTemplateCatalog::RuntimeTemplateCatalog(std::filesystem::path exportTemplatesRoot)
    : root_(std::move(exportTemplatesRoot)) {}

RuntimeTemplateInfo RuntimeTemplateCatalog::resolve(
    ExportTarget target, int projectFormatVersion,
    std::string_view packerAssetKeyId) const {
    RuntimeTemplateInfo info;
    const char* folder = (target == ExportTarget::WindowsX64) ? "windows-x64" : "";
    if (*folder == '\0') {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::MissingTemplate, "unsupported export target"));
        return info;
    }

    info.directory = root_ / folder;
    const auto manifestPath = info.directory / "runtime-template.json";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(manifestPath, ec)) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::MissingTemplate,
            "runtime-template.json missing", manifestPath));
        return info;
    }

    nlohmann::json json;
    try {
        std::ifstream in(manifestPath);
        in >> json;
    } catch (...) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::MissingTemplate,
            "runtime-template.json is not valid JSON", manifestPath));
        return info;
    }

    if (json.value("schemaVersion", 0) != 1) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::IncompatibleTemplate, "unknown template schemaVersion"));
        return info;
    }
    if (json.value("target", "") != folder) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::IncompatibleTemplate, "template target mismatch"));
        return info;
    }

    info.entryPoint = json.value("entryPoint", "");
    if (info.entryPoint.empty() || info.entryPoint.find("..") != std::string::npos
        || info.entryPoint.find('/') != std::string::npos
        || info.entryPoint.find('\\') != std::string::npos
        || info.entryPoint.find(':') != std::string::npos) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::IncompatibleTemplate, "invalid entryPoint"));
        return info;
    }

    info.engineVersion = json.value("engineVersion", "");
    info.runtimeBuildId = json.value("runtimeBuildId", "");
    info.assetKeyId = json.value("assetKeyId", "");
    info.supportsEncryptedArtcade = json.value("supportsEncryptedArtcade", false);
    if (json.contains("projectFormat")) {
        info.projectFormatMin = json["projectFormat"].value("minimum", 0);
        info.projectFormatMax = json["projectFormat"].value("maximum", 0);
    }

    if (!info.supportsEncryptedArtcade) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::IncompatibleTemplate,
            "template does not support encrypted .artcade"));
        return info;
    }
    if (projectFormatVersion < info.projectFormatMin
        || projectFormatVersion > info.projectFormatMax) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::IncompatibleTemplate,
            "project format incompatible with template"));
        return info;
    }
    if (info.assetKeyId != packerAssetKeyId) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::IncompatibleTemplate,
            "template assetKeyId does not match packer"));
        return info;
    }

    info.entryPointPath = info.directory / info.entryPoint;
    if (!std::filesystem::is_regular_file(info.entryPointPath, ec)) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::MissingTemplate,
            "template player binary missing — run scripts/refresh-export-templates.bat",
            info.entryPointPath));
        return info;
    }

    info.entryPointSize = std::filesystem::file_size(info.entryPointPath, ec);
    info.entryPointSha256 = ArtCade::sha256FileHex(info.entryPointPath.string());
    const auto& fileMeta = json["files"][info.entryPoint];
    const auto expectedSize = fileMeta.value("size", static_cast<std::uintmax_t>(0));
    const auto expectedSha = fileMeta.value("sha256", std::string{});
    if (expectedSize == 0 || expectedSha.empty()
        || expectedSize != info.entryPointSize
        || expectedSha != info.entryPointSha256) {
        info.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::MissingTemplate,
            "template player hash/size mismatch — refresh export templates",
            info.entryPointPath));
        return info;
    }

    info.ok = true;
    return info;
}

} // namespace ArtCade::EditorNative
