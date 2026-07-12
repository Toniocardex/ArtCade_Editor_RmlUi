#include "editor-native/model/path_confinement.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace ArtCade::EditorNative {

namespace {

std::filesystem::path portablePath(const std::filesystem::path& input) {
    std::string text = input.generic_u8string();
    std::replace(text.begin(), text.end(), '\\', '/');
    return std::filesystem::u8path(text);
}

bool looksLikeForeignAbsolutePath(const std::string& text) {
    return (text.size() >= 2
            && std::isalpha(static_cast<unsigned char>(text[0]))
            && text[1] == ':')
        || (text.size() >= 2 && text[0] == '/' && text[1] == '/');
}

bool componentContained(const std::filesystem::path& root,
                        const std::filesystem::path& candidate) {
    auto rootIt = root.begin();
    auto candidateIt = candidate.begin();
    for (; rootIt != root.end(); ++rootIt, ++candidateIt) {
        if (candidateIt == candidate.end() || *rootIt != *candidateIt) return false;
    }
    return true;
}

PathConfinementResult fail(std::string message) {
    return PathConfinementResult::failure(
        std::move(message),
        "Use a portable path inside the project folder (for example assets/images/file.png)");
}

} // namespace

bool isSafeProjectRelativePath(const std::filesystem::path& path, std::string* error) {
    const std::string raw = path.generic_u8string();
    const std::filesystem::path portable = portablePath(path);
    const auto reject = [&](const char* message) {
        if (error) *error = message;
        return false;
    };

    if (raw.empty()) return reject("Project path cannot be empty");
    if (portable.is_absolute() || portable.has_root_name()
        || portable.has_root_directory()
        || looksLikeForeignAbsolutePath(portable.generic_u8string())) {
        return reject("Project path must be relative");
    }
    // Besides drive syntax, ':' can address NTFS alternate data streams. It is
    // never part of the portable project-path grammar.
    if (portable.generic_u8string().find(':') != std::string::npos) {
        return reject("Project path cannot contain ':'");
    }
    for (const std::filesystem::path& component : portable) {
        if (component == "..") return reject("Project path cannot contain parent traversal");
    }
    if (portable.lexically_normal().empty() || portable.lexically_normal() == ".") {
        return reject("Project path must name a file or directory");
    }
    return true;
}

PathConfinementResult resolvePathInsideRoot(const std::filesystem::path& root,
                                            const std::filesystem::path& relativePath) {
    if (root.empty()) return fail("Project root cannot be empty");
    std::string relativeError;
    if (!isSafeProjectRelativePath(relativePath, &relativeError)) {
        return fail(std::move(relativeError));
    }

    std::error_code ec;
    const std::filesystem::path absoluteRoot = std::filesystem::absolute(root, ec);
    if (ec) return fail("Could not make the project root absolute: " + ec.message());
    const std::filesystem::path canonicalRoot =
        std::filesystem::weakly_canonical(absoluteRoot, ec);
    if (ec) return fail("Could not canonicalize the project root: " + ec.message());
    if (!std::filesystem::is_directory(canonicalRoot, ec) || ec) {
        return fail("Project root is not an accessible directory");
    }

    const std::filesystem::path portable = portablePath(relativePath).lexically_normal();
    const std::filesystem::path candidate =
        std::filesystem::weakly_canonical(canonicalRoot / portable, ec);
    if (ec) return fail("Could not canonicalize the project path: " + ec.message());
    if (!componentContained(canonicalRoot, candidate)) {
        return fail("Project path resolves outside the project root");
    }
    return PathConfinementResult::success(candidate);
}

} // namespace ArtCade::EditorNative
