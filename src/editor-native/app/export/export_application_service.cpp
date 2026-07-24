#include "editor-native/app/export/export_application_service.h"

#include "artcade-archive/archive_util.h"
#include "editor-native/app/editor_coordinator.h"
#include "editor-native/app/export/export_transaction.h"
#include "editor-native/app/export/project_pack_service.h"
#include "editor-native/app/export/runtime_project_preflight.h"
#include "core/project-current-format.h"

#include <fstream>
#include <nlohmann/json.hpp>

namespace ArtCade::EditorNative {
namespace {

bool copyFileBinary(const std::filesystem::path& from, const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::copy_file(from, to,
                               std::filesystem::copy_options::overwrite_existing, ec);
    return !ec;
}

} // namespace

ExportApplicationService::ExportApplicationService(ExportApplicationDeps deps)
    : deps_(std::move(deps)) {}

ExportResult ExportApplicationService::exportWindows(
    const ExportRequest& request,
    const ExportContext& context,
    const ProjectDocument& document,
    EditorCoordinator& coordinator) const {
    ExportResult result;
    result.stage = ExportStage::Preflight;

    if (coordinator.isPlaying()) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::Playing, "Export is unavailable during Play"));
        result.stage = ExportStage::Failed;
        return result;
    }

    if (deps_.projectFilesStableForSnapshot && !deps_.projectFilesStableForSnapshot()) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::ProjectWriteInProgress,
            "Project files are being generated. Wait before exporting."));
        result.stage = ExportStage::Failed;
        return result;
    }

    if (context.projectFile.empty() || context.projectRoot.empty()) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::ProjectUnsaved, "Project must be saved before Export"));
        result.stage = ExportStage::Failed;
        return result;
    }

    if (request.destinationDirectory.empty()) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::InvalidDestination, "Export destination is empty"));
        result.stage = ExportStage::Failed;
        return result;
    }

    if (destinationIsInsideProjectRoot(request.destinationDirectory, context.projectRoot)
        || destinationIsInsideProjectRoot(
               request.destinationDirectory / request.productName, context.projectRoot)) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::InvalidDestination,
            "Export destination is inside the project folder",
            request.destinationDirectory));
        result.stage = ExportStage::Failed;
        return result;
    }

    bool nameChanged = false;
    const std::string productFile = normalizeProductFileName(request.productName, &nameChanged);
    if (productFile.empty()) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::InvalidProductName, "Invalid product name"));
        result.stage = ExportStage::Failed;
        return result;
    }
    if (nameChanged) {
        result.diagnostics.push_back(makeExportWarning(
            ExportDiagnosticCode::InvalidProductName,
            "Product filename was normalized for the filesystem"));
        // Keep warning code as informational via severity; message is enough.
        result.diagnostics.back().severity = ExportDiagnosticSeverity::Warning;
    }

    const ExportDomainEligibility domain = coordinator.evaluateExportDomainEligibility();
    if (!domain.allowed) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  domain.diagnostics.begin(), domain.diagnostics.end());
        result.stage = ExportStage::Failed;
        return result;
    }

    const RuntimeProjectPreflightResult preflight = prepareRuntimeProjectSnapshot(document);
    if (!preflight.ok) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  preflight.diagnostics.begin(),
                                  preflight.diagnostics.end());
        result.stage = ExportStage::Failed;
        return result;
    }

    // Editor projects default mainScriptPath to scripts/main.luac even when
    // neither .luac nor .lua exists. Clear it in the packed JSON so the player
    // does not hard-require a missing legacy entry (Script Assets / Logic Board
    // cover gameplay).
    std::string packedProjectJson = preflight.canonicalProjectJson;
    try {
        nlohmann::json parsed = nlohmann::json::parse(packedProjectJson);
        const std::string mainPath = parsed.value("mainScriptPath", "");
        if (!mainPath.empty()) {
            std::error_code ec;
            const auto mainAbs =
                context.projectRoot / std::filesystem::u8path(mainPath);
            bool present = std::filesystem::is_regular_file(mainAbs, ec);
            if (!present) {
                std::string luaAlt = mainPath;
                constexpr const char kLuac[] = ".luac";
                if (luaAlt.size() >= sizeof(kLuac) - 1
                    && luaAlt.compare(luaAlt.size() - (sizeof(kLuac) - 1),
                                      sizeof(kLuac) - 1, kLuac)
                           == 0) {
                    luaAlt.replace(luaAlt.size() - (sizeof(kLuac) - 1),
                                   sizeof(kLuac) - 1, ".lua");
                    present = std::filesystem::is_regular_file(
                        context.projectRoot / std::filesystem::u8path(luaAlt), ec);
                }
            }
            if (!present) {
                parsed["mainScriptPath"] = "";
                packedProjectJson = parsed.dump(2);
            }
        }
    } catch (...) {
        // Keep canonical JSON if rewrite fails; player soft-skips missing main.
    }

    RuntimeTemplateCatalog catalog(deps_.exportTemplatesRoot);
    const RuntimeTemplateInfo templateInfo = catalog.resolve(
        ExportTarget::WindowsX64,
        context.projectFormatVersion != 0
            ? context.projectFormatVersion
            : ProjectJson::kCurrentProjectFormatVersion,
        ArtCade::artcadeAssetKeyId());
    if (!templateInfo.ok) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  templateInfo.diagnostics.begin(),
                                  templateInfo.diagnostics.end());
        result.stage = ExportStage::Failed;
        return result;
    }

    const std::filesystem::path finalDir =
        request.destinationDirectory / productFile;
    ExportTransaction tx = ExportTransaction::begin(finalDir);
    if (tx.stagingDirectory().empty()
        || !std::filesystem::exists(tx.stagingDirectory())) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::DestinationUnavailable,
            "failed to create export staging directory", finalDir));
        result.stage = ExportStage::Failed;
        return result;
    }

    result.stage = ExportStage::PackingProject;
    ExportProjectSnapshot snapshot;
    snapshot.projectJson = packedProjectJson;
    snapshot.projectName = context.projectName.empty() ? productFile : context.projectName;
    snapshot.startSceneId = document.startSceneId();
    snapshot.documentRevision = context.projectRevision;
    snapshot.projectFormatVersion = context.projectFormatVersion;

    const auto archivePath = tx.stagingDirectory() / "game.artcade";
    const ProjectPackServiceResult packed =
        ProjectPackService{}.packAllowlist(snapshot, context.projectRoot, archivePath);
    if (!packed.ok) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  packed.diagnostics.begin(), packed.diagnostics.end());
        result.stage = ExportStage::Failed;
        return result;
    }

    result.stage = ExportStage::PreparingPlayer;
    const auto exePath = tx.stagingDirectory() / (productFile + ".exe");
    if (!copyFileBinary(templateInfo.entryPointPath, exePath)) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::MissingTemplate,
            "failed to copy player template", templateInfo.entryPointPath));
        result.stage = ExportStage::Failed;
        return result;
    }

    const std::string copiedSha = ArtCade::sha256FileHex(exePath.string());
    if (copiedSha != templateInfo.entryPointSha256) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::MissingTemplate,
            "copied player hash mismatch"));
        result.stage = ExportStage::Failed;
        return result;
    }

    result.stage = ExportStage::Verifying;
    nlohmann::json buildManifest = {
        {"format", "artcade-export"},
        {"formatVersion", 1},
        {"target", "windows-x64"},
        {"productName", productFile},
        {"projectId", context.projectId},
        {"projectRevision", context.projectRevision},
        {"projectFormatVersion", snapshot.projectFormatVersion},
        {"engineVersion", templateInfo.engineVersion},
        {"runtimeTemplateVersion", 1},
        {"runtimeBuildId", templateInfo.runtimeBuildId},
        {"assetKeyId", templateInfo.assetKeyId},
        {"artifacts", {
            {productFile + ".exe", {
                {"size", templateInfo.entryPointSize},
                {"sha256", templateInfo.entryPointSha256},
            }},
            {"game.artcade", {
                {"size", packed.archiveSize},
                {"sha256", packed.sha256},
            }},
        }},
    };
    {
        std::ofstream out(tx.stagingDirectory() / "build-manifest.json");
        out << buildManifest.dump(2) << "\n";
        if (!out) {
            result.diagnostics.push_back(makeExportError(
                ExportDiagnosticCode::DestinationUnavailable,
                "failed to write build-manifest.json"));
            result.stage = ExportStage::Failed;
            return result;
        }
    }

    if (request.replaceExisting) {
        result.diagnostics.push_back({
            ExportDiagnosticSeverity::Warning,
            ExportDiagnosticCode::DestinationUnavailable,
            "Existing export will be replaced",
            finalDir,
        });
    }

    ExportResult committed = tx.commit(request.replaceExisting);
    if (!committed.ok) {
        result.diagnostics.insert(result.diagnostics.end(),
                                  committed.diagnostics.begin(),
                                  committed.diagnostics.end());
        result.stage = ExportStage::Failed;
        return result;
    }

    result.ok = true;
    result.stage = ExportStage::Completed;
    result.outputDirectory = committed.outputDirectory;
    result.artifacts.push_back({
        committed.outputDirectory / (productFile + ".exe"),
        templateInfo.entryPointSize,
        templateInfo.entryPointSha256,
    });
    result.artifacts.push_back({
        committed.outputDirectory / "game.artcade",
        packed.archiveSize,
        packed.sha256,
    });
    result.artifacts.push_back({
        committed.outputDirectory / "build-manifest.json",
        0,
        {},
    });
    result.diagnostics.insert(result.diagnostics.end(),
                              committed.diagnostics.begin(),
                              committed.diagnostics.end());
    return result;
}

} // namespace ArtCade::EditorNative
