#include "artcade-pack/project_packer.h"

#include "artcade-archive/archive_util.h"
#include "artcade-crypto.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace ArtCade {
namespace {

std::string normalizeArchivePath(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    while (!path.empty() && path.front() == '/') path.erase(path.begin());
    return path;
}

bool isReservedArchivePath(const std::string& path) {
    return path == "manifest.json" || path == "project.json";
}

bool pathHasDotDot(const std::string& path) {
    std::string component;
    for (char c : path) {
        if (c == '/') {
            if (component == "..") return true;
            component.clear();
        } else {
            component.push_back(c);
        }
    }
    return component == "..";
}

bool readAllBytes(const std::filesystem::path& path, std::vector<uint8_t>& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0) return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) {
        in.read(reinterpret_cast<char*>(out.data()), size);
        if (!in) return false;
    }
    return true;
}

bool writeAtomic(const std::filesystem::path& path, const std::vector<uint8_t>& data) {
    const auto tmp = path.string() + ".artcade_pack_tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        if (!data.empty()) {
            out.write(reinterpret_cast<const char*>(data.data()),
                      static_cast<std::streamsize>(data.size()));
        }
        if (!out) return false;
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

std::string isoUtcNow() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const std::time_t t = clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

ProjectPackResult ProjectPacker::pack(const ProjectPackRequest& request) const {
    ProjectPackResult result;
    if (request.outputFile.empty()) {
        result.error = "output path is empty";
        return result;
    }

    struct Prepared {
        std::string archivePath;
        std::vector<uint8_t> data;
        std::string sha;
    };
    std::vector<Prepared> prepared;
    std::unordered_set<std::string> seen;
    std::unordered_set<std::string> seenLower;
    bool hasProjectJson = false;

    for (const PackArchiveEntry& entry : request.entries) {
        const std::string archivePath = normalizeArchivePath(entry.archivePath);
        if (archivePath.empty() || pathHasDotDot(archivePath) ||
            archivePath.find(':') != std::string::npos) {
            result.error = "invalid archive path: " + entry.archivePath;
            return result;
        }
        if (isReservedArchivePath(archivePath) && archivePath != "project.json") {
            result.error = "reserved archive path cannot be supplied: " + archivePath;
            return result;
        }
        if (archivePath == "project.json") hasProjectJson = true;

        if (!seen.insert(archivePath).second) {
            result.error = "duplicate archive path: " + archivePath;
            return result;
        }
        const std::string lower = toLowerAscii(archivePath);
        if (!seenLower.insert(lower).second) {
            result.error = "case-colliding archive path: " + archivePath;
            return result;
        }

        Prepared item;
        item.archivePath = archivePath;
        if (std::holds_alternative<std::vector<uint8_t>>(entry.source)) {
            item.data = std::get<std::vector<uint8_t>>(entry.source);
        } else {
            const auto& path = std::get<std::filesystem::path>(entry.source);
            std::error_code ec;
            if (std::filesystem::is_symlink(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
                result.error = "source is not a regular file: " + path.u8string();
                return result;
            }
            if (!readAllBytes(path, item.data)) {
                result.error = "failed to read: " + path.u8string();
                return result;
            }
        }
        item.sha = sha256Hex(item.data);
        prepared.push_back(std::move(item));
    }

    if (!hasProjectJson) {
        result.error = "project.json entry is required";
        return result;
    }

    std::sort(prepared.begin(), prepared.end(),
              [](const Prepared& a, const Prepared& b) {
                  return a.archivePath < b.archivePath;
              });

    // Build packer manifest (entry 0), then all files in sorted order.
    std::ostringstream filesJson;
    filesJson << "{";
    bool first = true;
    for (const Prepared& item : prepared) {
        if (!first) filesJson << ",";
        first = false;
        filesJson << "\"" << item.archivePath << "\":\"" << item.sha << "\"";
    }
    filesJson << "}";

    std::ostringstream manifest;
    manifest << "{"
             << "\"format\":\"artcade\","
             << "\"version\":\"2.0.0\","
             << "\"projectName\":\"" << request.projectName << "\","
             << "\"projectVersion\":\"" << request.projectVersion << "\","
             << "\"licenseTier\":\"free\","
             << "\"created\":\"" << isoUtcNow() << "\","
             << "\"files\":" << filesJson.str()
             << "}";
    const std::string manifestText = manifest.str();

    std::vector<ZipWriteEntry> zipEntries;
    zipEntries.reserve(prepared.size() + 1);
    {
        ZipWriteEntry man;
        man.name = "manifest.json";
        man.data.assign(manifestText.begin(), manifestText.end());
        zipEntries.push_back(std::move(man));
    }
    for (const Prepared& item : prepared) {
        ZipWriteEntry z;
        z.name = item.archivePath;
        z.data = item.data;
        zipEntries.push_back(std::move(z));
    }

    std::vector<uint8_t> zipBytes;
    if (!zipWriteStore(zipEntries, zipBytes)) {
        result.error = "failed to build ZIP";
        return result;
    }

    std::vector<uint8_t> outBytes;
    if (request.encryption == PackEncryption::ReleaseEncrypted) {
        if (!artcadeEncryptArchive(zipBytes, outBytes)) {
            result.error = "failed to encrypt archive (ARTCADE1)";
            return result;
        }
        if (!artcadeArchiveIsEncrypted(outBytes)) {
            result.error = "encryption did not produce ARTCADE1 container";
            return result;
        }
    } else {
        outBytes = std::move(zipBytes);
    }

    std::error_code ec;
    if (request.outputFile.has_parent_path()) {
        std::filesystem::create_directories(request.outputFile.parent_path(), ec);
    }
    if (!writeAtomic(request.outputFile, outBytes)) {
        result.error = "failed to write output: " + request.outputFile.u8string();
        return result;
    }

    result.ok = true;
    result.archiveSize = outBytes.size();
    result.sha256 = sha256Hex(outBytes);
    return result;
}

} // namespace ArtCade
