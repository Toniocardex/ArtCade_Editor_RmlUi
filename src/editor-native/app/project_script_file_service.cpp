#include "editor-native/app/project_script_file_service.h"

#include "editor-native/app/project_file.h"
#include "editor-native/model/script_source_stamp.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
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
#include <fcntl.h>
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#include <errno.h>
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

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
    result.hash = scriptSourceStamp(source.value).hash;
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

PathConfinementResult ProjectScriptFileService::confineAbsolutePath(
    const std::filesystem::path& absolutePath) const {
    if (!projectRoot_.is_absolute()) {
        return PathConfinementResult::failure(
            "Script project root must be absolute",
            "Open or save the project before accessing script files");
    }
    if (!absolutePath.is_absolute()) {
        return PathConfinementResult::failure(
            "Script absolute path must be absolute",
            "Resolve the path under the project root first");
    }
    std::error_code ec;
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(projectRoot_, ec);
    if (ec) {
        return PathConfinementResult::failure(
            "Could not canonicalize the project root: " + ec.message(),
            "Open or save the project before accessing script files");
    }
    const std::filesystem::path canonicalPath =
        std::filesystem::weakly_canonical(absolutePath, ec);
    if (ec) {
        return PathConfinementResult::failure(
            "Could not canonicalize the script path: " + ec.message(),
            "Keep script files inside the project folder");
    }
    const std::filesystem::path relative = canonicalPath.lexically_relative(canonicalRoot);
    if (relative.empty() || relative.is_absolute()
        || (!relative.begin()->empty() && *relative.begin() == "..")) {
        return PathConfinementResult::failure(
            "Script path escapes the project root",
            "Keep script files inside the project folder");
    }
    return PathConfinementResult::success(canonicalPath);
}

namespace {

bool pathHasLuaExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".lua";
}

ScriptFileResult<ScriptSourceBytes> readRawBytesAt(
    const std::filesystem::path& path, bool requireLuaExtension) {
    if (requireLuaExtension && !pathHasLuaExtension(path)) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            "Script source must be a .lua file");
    }
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            "Could not inspect script source: " + ec.message());
    }
    if (!exists) {
        ScriptSourceBytes missing;
        missing.existed = false;
        return ScriptFileResult<ScriptSourceBytes>::success(std::move(missing));
    }
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            "Script source file is missing or unreadable");
    }
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            "Could not inspect script source size");
    }
    if (size > kMaxScriptSourceBytes) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            "Script source exceeds the 1 MiB limit");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            "Could not open script source");
    }
    ScriptSourceBytes out;
    out.existed = true;
    out.bytes.resize(static_cast<std::size_t>(size));
    if (!out.bytes.empty()) {
        input.read(reinterpret_cast<char*>(out.bytes.data()),
                   static_cast<std::streamsize>(out.bytes.size()));
        if (!input || input.gcount() != static_cast<std::streamsize>(out.bytes.size())) {
            return ScriptFileResult<ScriptSourceBytes>::failure(
                "Could not read the complete script source");
        }
    }
    return ScriptFileResult<ScriptSourceBytes>::success(std::move(out));
}

ScriptFileResult<std::filesystem::path> writeRawBytesNoReplaceAt(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes,
    bool requireLuaExtension) {
    if (requireLuaExtension && !pathHasLuaExtension(path)) {
        return ScriptFileResult<std::filesystem::path>::failure(
            "Script source must be a .lua file");
    }
    if (bytes.size() > kMaxScriptSourceBytes) {
        return ScriptFileResult<std::filesystem::path>::failure(
            "Script source exceeds the 1 MiB limit");
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return ScriptFileResult<std::filesystem::path>::failure(
            "Could not create script folder: " + ec.message());
    }
#if defined(_WIN32)
    int descriptor = -1;
    const errno_t opened = _wsopen_s(
        &descriptor, path.c_str(),
        _O_BINARY | _O_WRONLY | _O_CREAT | _O_EXCL,
        _SH_DENYRW, _S_IREAD | _S_IWRITE);
    if (opened != 0) {
        return ScriptFileResult<std::filesystem::path>::failure(
            std::generic_category().message(static_cast<int>(opened)));
    }
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const unsigned int count = static_cast<unsigned int>(std::min<std::size_t>(
            bytes.size() - offset,
            static_cast<std::size_t>((std::numeric_limits<int>::max)())));
        const int written = _write(descriptor, bytes.data() + offset, count);
        if (written <= 0) {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    const int closed = _close(descriptor);
    if (!ok || closed != 0) {
        std::error_code removeEc;
        std::filesystem::remove(path, removeEc);
        return ScriptFileResult<std::filesystem::path>::failure(
            "Could not write script source bytes");
    }
#else
    const int descriptor = ::open(
        path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        return ScriptFileResult<std::filesystem::path>::failure(
            std::system_category().message(errno));
    }
    std::size_t offset = 0;
    bool ok = true;
    while (offset < bytes.size()) {
        const ssize_t written = ::write(
            descriptor, bytes.data() + offset, bytes.size() - offset);
        if (written <= 0) {
            ok = false;
            break;
        }
        offset += static_cast<std::size_t>(written);
    }
    const int closed = ::close(descriptor);
    if (!ok || closed != 0) {
        std::error_code removeEc;
        std::filesystem::remove(path, removeEc);
        return ScriptFileResult<std::filesystem::path>::failure(
            "Could not write script source bytes");
    }
#endif
    return ScriptFileResult<std::filesystem::path>::success(path);
}

} // namespace

ScriptFileResult<ScriptSourceBytes> ProjectScriptFileService::readRawScriptIfExists(
    const std::filesystem::path& relativePath) const {
    const PathConfinementResult resolved = resolveProjectRelativePath(relativePath);
    if (!resolved.ok) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            resolved.error + ". " + resolved.remediation);
    }
    return readRawBytesAt(resolved.value, true);
}

ScriptFileResult<std::filesystem::path> ProjectScriptFileService::writeRawScriptNoReplace(
    const std::filesystem::path& relativePath,
    const std::vector<std::uint8_t>& bytes) const {
    const PathConfinementResult resolved = resolveProjectRelativePath(relativePath);
    if (!resolved.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(
            resolved.error + ". " + resolved.remediation);
    }
    return writeRawBytesNoReplaceAt(resolved.value, bytes, true);
}

ScriptFileResult<ScriptSourceBytes> ProjectScriptFileService::readRawAbsoluteIfExists(
    const std::filesystem::path& absolutePath,
    bool requireLuaExtension) const {
    const PathConfinementResult confined = confineAbsolutePath(absolutePath);
    if (!confined.ok) {
        return ScriptFileResult<ScriptSourceBytes>::failure(
            confined.error + ". " + confined.remediation);
    }
    return readRawBytesAt(confined.value, requireLuaExtension);
}

ScriptFileResult<std::filesystem::path> ProjectScriptFileService::writeRawAbsoluteNoReplace(
    const std::filesystem::path& absolutePath,
    const std::vector<std::uint8_t>& bytes,
    bool requireLuaExtension) const {
    const PathConfinementResult confined = confineAbsolutePath(absolutePath);
    if (!confined.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(
            confined.error + ". " + confined.remediation);
    }
    return writeRawBytesNoReplaceAt(confined.value, bytes, requireLuaExtension);
}

ScriptFileResult<std::filesystem::path> ProjectScriptFileService::moveAbsoluteNoReplace(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) const {
    const PathConfinementResult confinedSource = confineAbsolutePath(source);
    if (!confinedSource.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(
            confinedSource.error + ". " + confinedSource.remediation);
    }
    const PathConfinementResult confinedDest = confineAbsolutePath(destination);
    if (!confinedDest.ok) {
        return ScriptFileResult<std::filesystem::path>::failure(
            confinedDest.error + ". " + confinedDest.remediation);
    }
    std::error_code ec;
    if (std::filesystem::exists(confinedDest.value, ec) || ec) {
        if (ec) {
            return ScriptFileResult<std::filesystem::path>::failure(
                "Could not inspect script destination: " + ec.message());
        }
        return ScriptFileResult<std::filesystem::path>::failure(
            "Script destination already exists");
    }
#if defined(_WIN32)
    constexpr unsigned long kWriteThrough = 0x8u;
    if (MoveFileExW(confinedSource.value.c_str(), confinedDest.value.c_str(),
                    kWriteThrough)) {
        return ScriptFileResult<std::filesystem::path>::success(confinedDest.value);
    }
    return ScriptFileResult<std::filesystem::path>::failure(
        std::system_category().message(static_cast<int>(GetLastError())));
#else
    std::error_code linkError;
    std::filesystem::create_hard_link(
        confinedSource.value, confinedDest.value, linkError);
    if (linkError) {
        return ScriptFileResult<std::filesystem::path>::failure(linkError.message());
    }
    std::error_code removeError;
    std::filesystem::remove(confinedSource.value, removeError);
    if (!removeError) {
        return ScriptFileResult<std::filesystem::path>::success(confinedDest.value);
    }
    std::error_code rollbackError;
    std::filesystem::remove(confinedDest.value, rollbackError);
    return ScriptFileResult<std::filesystem::path>::failure(
        removeError.message()
        + (rollbackError ? "; rollback failed: " + rollbackError.message()
                         : std::string{}));
#endif
}

ScriptFileResult<bool> ProjectScriptFileService::removeAbsoluteIfExists(
    const std::filesystem::path& absolutePath) const {
    const PathConfinementResult confined = confineAbsolutePath(absolutePath);
    if (!confined.ok) {
        return ScriptFileResult<bool>::failure(
            confined.error + ". " + confined.remediation);
    }
    std::error_code ec;
    const bool removed = std::filesystem::remove(confined.value, ec);
    if (ec) {
        return ScriptFileResult<bool>::failure(
            "Could not remove script file: " + ec.message());
    }
    return ScriptFileResult<bool>::success(removed);
}

ScriptFileResult<bool> ProjectScriptFileService::removeScriptIfExists(
    const std::filesystem::path& relativePath) const {
    const PathConfinementResult resolved = resolveProjectRelativePath(relativePath);
    if (!resolved.ok) {
        return ScriptFileResult<bool>::failure(
            resolved.error + ". " + resolved.remediation);
    }
    if (!pathHasLuaExtension(resolved.value)) {
        return ScriptFileResult<bool>::failure("Script source must be a .lua file");
    }
    return removeAbsoluteIfExists(resolved.value);
}

} // namespace ArtCade::EditorNative
