#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>
#include <vector>

namespace ArtCade {

enum class PackEncryption {
    ReleaseEncrypted,
    DevelopmentPlaintext,
};

struct PackArchiveEntry {
    std::string archivePath;
    std::variant<std::filesystem::path, std::vector<std::uint8_t>> source;
};

struct ProjectPackRequest {
    std::string projectName;
    std::string projectVersion = "1.0.0";
    std::vector<PackArchiveEntry> entries;
    std::filesystem::path outputFile;
    PackEncryption encryption = PackEncryption::ReleaseEncrypted;
};

struct ProjectPackResult {
    bool ok = false;
    std::uintmax_t archiveSize = 0;
    std::string sha256;
    std::string error;
};

class ProjectPacker {
public:
    ProjectPackResult pack(const ProjectPackRequest& request) const;
};

} // namespace ArtCade
