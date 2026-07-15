#pragma once

#include "editor-native/model/path_confinement.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace ArtCade::EditorNative {

inline constexpr std::uintmax_t kMaxScriptSourceBytes = 1024u * 1024u;

template <class T>
struct ScriptFileResult {
    bool        ok = false;
    T           value{};
    std::string error;

    static ScriptFileResult success(T value) {
        ScriptFileResult out;
        out.ok = true;
        out.value = std::move(value);
        return out;
    }
    static ScriptFileResult failure(std::string error) {
        ScriptFileResult out;
        out.error = std::move(error);
        return out;
    }
};

struct ScriptFileFingerprint {
    std::uint64_t hash = 0;
    std::uintmax_t size = 0;
    std::filesystem::file_time_type modified{};

    bool operator==(const ScriptFileFingerprint& other) const {
        return hash == other.hash && size == other.size && modified == other.modified;
    }
};

// The only project-script filesystem boundary. It owns no document metadata
// and resolves every authored path relative to one explicit project root.
class ProjectScriptFileService {
public:
    explicit ProjectScriptFileService(std::filesystem::path projectRoot)
        : projectRoot_(std::move(projectRoot)) {}

    const std::filesystem::path& projectRoot() const { return projectRoot_; }

    PathConfinementResult resolveProjectRelativePath(
        const std::filesystem::path& relativePath) const;
    ScriptFileResult<std::string> readScript(
        const std::filesystem::path& relativePath) const;
    ScriptFileResult<std::string> readImportSource(
        const std::filesystem::path& sourcePath) const;
    ScriptFileResult<ScriptFileFingerprint> fingerprint(
        const std::filesystem::path& relativePath) const;
    ScriptFileResult<std::filesystem::path> writeScriptAtomically(
        const std::filesystem::path& relativePath, std::string text) const;

    static ScriptFileResult<std::string> normalizeUtf8Lua(std::string text);

private:
    std::filesystem::path projectRoot_;
};

} // namespace ArtCade::EditorNative

