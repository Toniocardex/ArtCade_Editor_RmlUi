#pragma once

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

struct FontLoadResult {
    bool ok = false;
    std::string error;
};

/**
 * Loads the native editor's required local UI fonts from the resource root.
 * @param resourceRoot absolute path to the copied `resources` directory.
 * @return success, or an error containing the missing/failing font path.
 */
[[nodiscard]]
FontLoadResult loadEditorFonts(const std::filesystem::path& resourceRoot);

} // namespace ArtCade::EditorNative
