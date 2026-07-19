#include "editor-native/app/project_destination.h"

#include "editor-native/app/project_file.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace ArtCade::EditorNative {

namespace {

constexpr const char* kRootMarkerName = ".artcade-root";
constexpr const char* kProjectExtension = ".artcade-project";

bool pathIsProjectFile(const std::filesystem::path& path) {
    return path.extension() == kProjectExtension;
}

bool directoryIsEmpty(const std::filesystem::path& directory, std::error_code& ec) {
    const std::filesystem::directory_iterator end;
    std::filesystem::directory_iterator it(directory, ec);
    if (ec) return false;
    return it == end;
}

std::vector<std::filesystem::path> listTopLevelProjectFiles(
    const std::filesystem::path& projectRoot, std::error_code& ec) {
    std::vector<std::filesystem::path> projects;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(projectRoot, ec)) {
        if (ec) return {};
        if (!entry.is_regular_file(ec) || ec) {
            if (ec) return {};
            continue;
        }
        if (pathIsProjectFile(entry.path())) projects.push_back(entry.path());
    }
    return projects;
}

bool markerLooksValid(const std::filesystem::path& markerPath) {
    std::ifstream in(markerPath, std::ios::binary);
    if (!in) return false;
    std::string text((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return text.find("\"artcade-project-root\"") != std::string::npos
        || text.find("artcade-project-root") != std::string::npos;
}

} // namespace

std::string makeNewProjectId() {
    std::random_device entropy;
    std::mt19937_64 generator{
        (static_cast<std::uint64_t>(entropy()) << 32)
        ^ static_cast<std::uint64_t>(entropy())
        ^ static_cast<std::uint64_t>(
              std::chrono::steady_clock::now().time_since_epoch().count())};
    std::uniform_int_distribution<std::uint64_t> distribute;
    std::ostringstream out;
    out << "project-" << std::hex << distribute(generator) << distribute(generator);
    return out.str();
}

bool isReplaceableArtCadeProjectRoot(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& destinationFile) {
    std::error_code ec;
    if (!std::filesystem::is_directory(projectRoot, ec) || ec) return false;
    if (!std::filesystem::is_regular_file(destinationFile, ec) || ec) return false;
    if (!pathIsProjectFile(destinationFile)) return false;

    const std::filesystem::path marker = projectRoot / kRootMarkerName;
    const bool hasMarker = std::filesystem::is_regular_file(marker, ec) && !ec;
    if (hasMarker && !markerLooksValid(marker)) return false;

    const std::vector<std::filesystem::path> projects =
        listTopLevelProjectFiles(projectRoot, ec);
    if (ec || projects.empty()) return false;

    bool destinationListed = false;
    for (const std::filesystem::path& project : projects) {
        std::error_code eq;
        if (std::filesystem::equivalent(project, destinationFile, eq) && !eq) {
            destinationListed = true;
            break;
        }
        if (project.filename() == destinationFile.filename()) {
            destinationListed = true;
            break;
        }
    }
    if (!destinationListed) return false;

    // Ambiguous multi-project folders are not auto-replaceable.
    if (projects.size() > 1) return false;
    return true;
}

ProjectDestinationInspection inspectNewProjectDestination(
    const std::filesystem::path& destinationFile) {
    ProjectDestinationInspection out;
    std::error_code ec;
    out.projectFile = std::filesystem::absolute(destinationFile, ec).lexically_normal();
    if (ec) {
        out.kind = ProjectDestinationKind::OccupiedGeneric;
        out.detail = "Could not resolve project destination: " + ec.message();
        return out;
    }
    out.projectRoot = out.projectFile.parent_path();
    if (out.projectRoot.empty()) {
        out.kind = ProjectDestinationKind::OccupiedGeneric;
        out.detail = "Project destination is missing a project folder";
        return out;
    }

    if (!std::filesystem::exists(out.projectRoot, ec)) {
        if (ec) {
            out.kind = ProjectDestinationKind::OccupiedGeneric;
            out.detail = "Could not inspect project folder: " + ec.message();
            return out;
        }
        out.kind = ProjectDestinationKind::Missing;
        return out;
    }
    if (!std::filesystem::is_directory(out.projectRoot, ec) || ec) {
        out.kind = ProjectDestinationKind::OccupiedGeneric;
        out.detail = "Project destination parent is not a folder";
        return out;
    }

    if (directoryIsEmpty(out.projectRoot, ec)) {
        if (ec) {
            out.kind = ProjectDestinationKind::OccupiedGeneric;
            out.detail = "Could not inspect project folder: " + ec.message();
            return out;
        }
        out.kind = ProjectDestinationKind::Empty;
        return out;
    }

    if (isReplaceableArtCadeProjectRoot(out.projectRoot, out.projectFile)) {
        out.kind = ProjectDestinationKind::ReplaceableArtCade;
        return out;
    }

    out.kind = ProjectDestinationKind::OccupiedGeneric;
    out.detail =
        "The selected folder is not empty and is not a replaceable ArtCade project root. "
        "Choose another folder.";
    return out;
}

bool writeArtCadeRootMarker(const std::filesystem::path& projectRoot,
                            const std::string& projectId,
                            std::string& error) {
    if (projectId.empty()) {
        error = "Project id is required for the ArtCade root marker";
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(projectRoot, ec);
    if (ec) {
        error = "Could not create project folder for root marker: " + ec.message();
        return false;
    }
    const std::filesystem::path markerPath = projectRoot / kRootMarkerName;
    std::ostringstream body;
    body << "{\n"
         << "  \"kind\": \"artcade-project-root\",\n"
         << "  \"projectId\": \"" << projectId << "\",\n"
         << "  \"format\": 1\n"
         << "}\n";
    const ProjectTextFileResult written =
        writeProjectTextFileAtomically(markerPath, body.str());
    if (!written.ok) {
        error = written.error.message;
        return false;
    }
    return true;
}

std::filesystem::path makeSiblingProjectBackupPath(
    const std::filesystem::path& projectRoot) {
    static std::atomic<std::uint64_t> token{1};
    const std::uint64_t id = token.fetch_add(1, std::memory_order_relaxed);
    const std::filesystem::path parent = projectRoot.parent_path();
    const std::string name = "." + projectRoot.filename().string()
        + ".artcade-replace-" + std::to_string(id);
    return parent / name;
}

bool moveDirectoryNoReplace(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(source, ec) || ec) {
        error = ec ? ec.message() : std::string{"Source folder does not exist"};
        return false;
    }
    if (std::filesystem::exists(destination, ec) || ec) {
        if (ec) {
            error = "Could not inspect backup destination: " + ec.message();
            return false;
        }
        error = "Backup destination already exists";
        return false;
    }
#if defined(_WIN32)
    if (!MoveFileExW(source.c_str(), destination.c_str(), 0)) {
        error = std::system_category().message(static_cast<int>(GetLastError()));
        return false;
    }
    return true;
#else
    std::filesystem::rename(source, destination, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
#endif
}

bool removeDirectoryRecursively(const std::filesystem::path& path,
                                std::string& error) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}

} // namespace ArtCade::EditorNative
