#pragma once

#include <filesystem>
#include <string>
#include <utility>

namespace ArtCade::EditorNative {

struct PathConfinementResult {
    bool                  ok = false;
    std::filesystem::path value;
    std::string           error;
    std::string           remediation;

    static PathConfinementResult success(std::filesystem::path path) {
        return {true, std::move(path), {}, {}};
    }

    static PathConfinementResult failure(std::string message, std::string fix) {
        return {false, {}, std::move(message), std::move(fix)};
    }
};

// Project-authored paths are relative and may never contain traversal. Both
// separator styles are accepted at the boundary and normalized to portable
// forward-slash syntax. This check performs no filesystem access and is safe at
// Command and deserialize/validate boundaries.
bool isSafeProjectRelativePath(const std::filesystem::path& path,
                               std::string* error = nullptr);

// Resolve a project-authored relative path under an existing directory root.
// The returned path is absolute/canonical (including the deepest existing
// parent), and is accepted only when its components remain below the canonical
// root. Existing symlinks, Windows junctions and other reparse points are thus
// resolved before containment is decided. Missing leaf paths are allowed.
PathConfinementResult resolvePathInsideRoot(const std::filesystem::path& root,
                                            const std::filesystem::path& relativePath);

} // namespace ArtCade::EditorNative
