#pragma once

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

// Classification of a New Project destination folder (parent of the
// `.artcade-project` file). New Project never silently reuses prior contents.
enum class ProjectDestinationKind {
    Missing,            // create the root and proceed
    Empty,              // proceed into the empty folder
    ReplaceableArtCade, // recognized ArtCade root; requires explicit replace
    OccupiedGeneric,    // refuse — choose another folder
};

struct ProjectDestinationInspection {
    ProjectDestinationKind kind = ProjectDestinationKind::OccupiedGeneric;
    std::filesystem::path  projectRoot;
    std::filesystem::path  projectFile;
    std::string            detail;
};

// Inspect the destination `.artcade-project` path. Does not mutate the filesystem.
ProjectDestinationInspection inspectNewProjectDestination(
    const std::filesystem::path& destinationFile);

// True when the folder is a recognized ArtCade project root for `destinationFile`.
bool isReplaceableArtCadeProjectRoot(
    const std::filesystem::path& projectRoot,
    const std::filesystem::path& destinationFile);

std::string makeNewProjectId();

bool writeArtCadeRootMarker(const std::filesystem::path& projectRoot,
                            const std::string& projectId,
                            std::string& error);

std::filesystem::path makeSiblingProjectBackupPath(
    const std::filesystem::path& projectRoot);

bool moveDirectoryNoReplace(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            std::string& error);

bool removeDirectoryRecursively(const std::filesystem::path& path,
                                std::string& error);

} // namespace ArtCade::EditorNative
