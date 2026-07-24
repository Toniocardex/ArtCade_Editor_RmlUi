#include "editor-native/app/export/project_pack_service.h"

#include "artcade-pack/project_packer.h"
#include "editor-native/model/path_confinement.h"

#include <fstream>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {
namespace {

bool pathInsideRoot(const std::filesystem::path& root, const std::filesystem::path& file) {
    std::error_code ec;
    const auto relative = std::filesystem::relative(file, root, ec);
    if (ec || relative.empty() || *relative.begin() == "..") return false;
    const PathConfinementResult resolved = resolvePathInsideRoot(root, relative);
    return resolved.ok;
}

bool collectTree(const std::filesystem::path& root,
                 const std::filesystem::path& relativeDir,
                 const std::string& archivePrefix,
                 std::vector<ArtCade::PackArchiveEntry>& out,
                 std::vector<ExportDiagnostic>& diagnostics) {
    const auto absDir = root / relativeDir;
    std::error_code ec;
    if (!std::filesystem::exists(absDir, ec)) return true;
    if (!std::filesystem::is_directory(absDir, ec)) {
        diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::InvalidAsset,
            archivePrefix + " is not a directory", absDir));
        return false;
    }

    for (std::filesystem::recursive_directory_iterator it(absDir, ec), end;
         it != end; it.increment(ec)) {
        if (ec) {
            diagnostics.push_back(makeExportError(
                ExportDiagnosticCode::InvalidAsset,
                "failed walking " + archivePrefix + ": " + ec.message(), absDir));
            return false;
        }
        const auto status = it->status(ec);
        if (ec) continue;
        if (std::filesystem::is_symlink(it->path(), ec)) {
            diagnostics.push_back(makeExportError(
                ExportDiagnosticCode::InvalidAsset,
                "symlink rejected", it->path()));
            return false;
        }
        if (!std::filesystem::is_regular_file(status)) continue;

        if (!pathInsideRoot(root, it->path())) {
            diagnostics.push_back(makeExportError(
                ExportDiagnosticCode::InvalidAsset,
                "asset path escapes project root", it->path()));
            return false;
        }
        const auto rel = std::filesystem::relative(it->path(), root, ec);
        if (ec) {
            diagnostics.push_back(makeExportError(
                ExportDiagnosticCode::InvalidAsset,
                "failed to relativize asset path", it->path()));
            return false;
        }
        ArtCade::PackArchiveEntry entry;
        entry.archivePath = rel.generic_u8string();
        entry.source = it->path();
        out.push_back(std::move(entry));
    }
    return true;
}

} // namespace

ProjectPackServiceResult ProjectPackService::packAllowlist(
    const ExportProjectSnapshot& snapshot,
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& outputArtcade) const {
    ProjectPackServiceResult result;

    ArtCade::ProjectPackRequest request;
    request.projectName = snapshot.projectName;
    request.outputFile = outputArtcade;
    request.encryption = ArtCade::PackEncryption::ReleaseEncrypted;

    {
        ArtCade::PackArchiveEntry project;
        project.archivePath = "project.json";
        project.source = std::vector<std::uint8_t>(snapshot.projectJson.begin(),
                                                   snapshot.projectJson.end());
        request.entries.push_back(std::move(project));
    }

    if (!collectTree(projectRoot, "assets", "assets", request.entries, result.diagnostics))
        return result;
    if (!collectTree(projectRoot, "scripts", "scripts", request.entries, result.diagnostics))
        return result;

    const auto gameJson = projectRoot / "game.json";
    std::error_code ec;
    if (std::filesystem::is_regular_file(gameJson, ec)) {
        if (std::filesystem::is_symlink(gameJson, ec) || !pathInsideRoot(projectRoot, gameJson)) {
            result.diagnostics.push_back(makeExportError(
                ExportDiagnosticCode::InvalidAsset,
                "game.json rejected", gameJson));
            return result;
        }
        ArtCade::PackArchiveEntry entry;
        entry.archivePath = "game.json";
        entry.source = gameJson;
        request.entries.push_back(std::move(entry));
    }

    const ArtCade::ProjectPackResult packed = ArtCade::ProjectPacker{}.pack(request);
    if (!packed.ok) {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::RuntimeValidationFailed,
            packed.error.empty() ? "pack failed" : packed.error));
        return result;
    }

    std::ifstream in(outputArtcade, std::ios::binary);
    std::vector<std::uint8_t> header(8);
    in.read(reinterpret_cast<char*>(header.data()), 8);
    if (!in || std::string(header.begin(), header.end()) != "ARTCADE1") {
        result.diagnostics.push_back(makeExportError(
            ExportDiagnosticCode::RuntimeValidationFailed,
            "packed archive is not an ARTCADE1 container", outputArtcade));
        std::filesystem::remove(outputArtcade, ec);
        return result;
    }

    result.ok = true;
    result.archivePath = outputArtcade;
    result.archiveSize = packed.archiveSize;
    result.sha256 = packed.sha256;
    return result;
}

} // namespace ArtCade::EditorNative
