#include "editor-native/app/project_script_file_service.h"

#include "editor-native/app/project_file.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <system_error>

namespace ArtCade::EditorNative {

namespace {

bool hasLuaExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".lua";
}

bool isValidUtf8(const std::string& text) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = bytes[i++];
        if (lead <= 0x7f) continue;
        int continuation = 0;
        std::uint32_t codepoint = 0;
        if ((lead & 0xe0) == 0xc0) { continuation = 1; codepoint = lead & 0x1f; }
        else if ((lead & 0xf0) == 0xe0) { continuation = 2; codepoint = lead & 0x0f; }
        else if ((lead & 0xf8) == 0xf0) { continuation = 3; codepoint = lead & 0x07; }
        else return false;
        if (i + static_cast<std::size_t>(continuation) > text.size()) return false;
        for (int n = 0; n < continuation; ++n) {
            const unsigned char byte = bytes[i++];
            if ((byte & 0xc0) != 0x80) return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((continuation == 1 && codepoint < 0x80)
            || (continuation == 2 && codepoint < 0x800)
            || (continuation == 3 && codepoint < 0x10000)
            || codepoint > 0x10ffff
            || (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

ScriptFileResult<std::string> readLuaFile(const std::filesystem::path& path) {
    if (!hasLuaExtension(path)) {
        return ScriptFileResult<std::string>::failure("Script source must be a .lua file");
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return ScriptFileResult<std::string>::failure("Script source file is missing or unreadable");
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) return ScriptFileResult<std::string>::failure("Could not inspect script source");
    if (size > kMaxScriptSourceBytes) {
        return ScriptFileResult<std::string>::failure("Script source exceeds the 1 MiB limit");
    }
    const auto modifiedBefore = std::filesystem::last_write_time(path, ec);
    if (ec) return ScriptFileResult<std::string>::failure("Could not inspect script source");
    std::ifstream input(path, std::ios::binary);
    if (!input) return ScriptFileResult<std::string>::failure("Could not open script source");
    std::string bytes(static_cast<std::size_t>(size), '\0');
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad()) {
        return ScriptFileResult<std::string>::failure("Could not read script source");
    }
    char extra = 0;
    if (input.get(extra)) {
        return ScriptFileResult<std::string>::failure(
            "Script source changed while being read or exceeds the 1 MiB limit");
    }
    const std::uintmax_t sizeAfter = std::filesystem::file_size(path, ec);
    if (ec) return ScriptFileResult<std::string>::failure("Could not verify script source");
    const auto modifiedAfter = std::filesystem::last_write_time(path, ec);
    if (ec || sizeAfter != size || sizeAfter != bytes.size()
        || modifiedAfter != modifiedBefore) {
        return ScriptFileResult<std::string>::failure(
            "Script source changed while being read; retry the operation");
    }
    return ProjectScriptFileService::normalizeUtf8Lua(std::move(bytes));
}

std::uint64_t fnv1a(const std::string& text) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

PathConfinementResult ProjectScriptFileService::resolveProjectRelativePath(
    const std::filesystem::path& relativePath) const {
    if (!projectRoot_.is_absolute()) {
        return PathConfinementResult::failure(
            "Script project root must be absolute",
            "Open or save the project before accessing script files");
    }
    return resolvePathInsideRoot(projectRoot_, relativePath);
}

ScriptFileResult<std::string> ProjectScriptFileService::normalizeUtf8Lua(std::string text) {
    if (text.size() > kMaxScriptSourceBytes) {
        return ScriptFileResult<std::string>::failure("Script source exceeds the 1 MiB limit");
    }
    if (text.size() >= 3
        && static_cast<unsigned char>(text[0]) == 0xef
        && static_cast<unsigned char>(text[1]) == 0xbb
        && static_cast<unsigned char>(text[2]) == 0xbf) {
        text.erase(0, 3);
    }
    if (!isValidUtf8(text)) {
        return ScriptFileResult<std::string>::failure("Script source must be valid UTF-8");
    }
    if (text.find('\0') != std::string::npos) {
        return ScriptFileResult<std::string>::failure(
            "Script source cannot contain NUL bytes");
    }
    std::string normalized;
    normalized.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            normalized.push_back('\n');
        } else {
            normalized.push_back(text[i]);
        }
    }
    return ScriptFileResult<std::string>::success(std::move(normalized));
}

ScriptFileResult<std::string> ProjectScriptFileService::readScript(
    const std::filesystem::path& relativePath) const {
    const PathConfinementResult resolved = resolveProjectRelativePath(relativePath);
    if (!resolved.ok) {
        return ScriptFileResult<std::string>::failure(
            resolved.error + ". " + resolved.remediation);
    }
    return readLuaFile(resolved.value);
}

ScriptFileResult<std::string> ProjectScriptFileService::readImportSource(
    const std::filesystem::path& sourcePath) const {
    if (!sourcePath.is_absolute()) {
        return ScriptFileResult<std::string>::failure(
            "Imported script path must be absolute; relative paths are never resolved against the process working directory");
    }
    return readLuaFile(sourcePath);
}

ScriptFileResult<ScriptFileFingerprint> ProjectScriptFileService::fingerprint(
    const std::filesystem::path& relativePath) const {
    const PathConfinementResult resolved = resolveProjectRelativePath(relativePath);
    if (!resolved.ok) {
        return ScriptFileResult<ScriptFileFingerprint>::failure(
            resolved.error + ". " + resolved.remediation);
    }
    const ScriptFileResult<std::string> source = readLuaFile(resolved.value);
    if (!source.ok) return ScriptFileResult<ScriptFileFingerprint>::failure(source.error);
    std::error_code ec;
    const auto modified = std::filesystem::last_write_time(resolved.value, ec);
    if (ec) {
        return ScriptFileResult<ScriptFileFingerprint>::failure(
            "Could not inspect script modification time");
    }
    ScriptFileFingerprint result;
    result.hash = fnv1a(source.value);
    result.size = source.value.size();
    result.modified = modified;
    return ScriptFileResult<ScriptFileFingerprint>::success(result);
}

ScriptFileResult<std::filesystem::path> ProjectScriptFileService::writeScriptAtomically(
    const std::filesystem::path& relativePath, std::string text) const {
    if (!hasLuaExtension(relativePath)) {
        return ScriptFileResult<std::filesystem::path>::failure(
            "Script source must be a .lua file");
    }
    ScriptFileResult<std::string> normalized = normalizeUtf8Lua(std::move(text));
    if (!normalized.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(normalized.error);
    }
    PathConfinementResult resolved = resolveProjectRelativePath(relativePath);
    if (!resolved.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(
            resolved.error + ". " + resolved.remediation);
    }
    std::error_code ec;
    std::filesystem::create_directories(resolved.value.parent_path(), ec);
    if (ec) {
        return ScriptFileResult<std::filesystem::path>::failure(
            "Could not create script folder: " + ec.message());
    }
    // Re-resolve after directory creation so a reparse point introduced at the
    // parent boundary cannot turn the write into a project-root escape.
    resolved = resolveProjectRelativePath(relativePath);
    if (!resolved.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(
            resolved.error + ". " + resolved.remediation);
    }
    const ProjectTextFileResult written =
        writeProjectTextFileAtomically(resolved.value, normalized.value);
    if (!written.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(written.error.message);
    }
    return ScriptFileResult<std::filesystem::path>::success(resolved.value);
}

} // namespace ArtCade::EditorNative
