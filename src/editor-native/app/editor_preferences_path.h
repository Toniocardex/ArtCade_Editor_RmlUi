#pragma once

#include <filesystem>
#include <optional>

namespace ArtCade::EditorNative {

// App-local preferences root: %LOCALAPPDATA%\ArtCade on Windows.
// nullopt when the platform folder cannot be resolved.
std::optional<std::filesystem::path> editorPreferencesDirectory();

// %LOCALAPPDATA%\ArtCade\recent-projects.json
std::optional<std::filesystem::path> recentProjectsPreferencesPath();

// %LOCALAPPDATA%\ArtCade\editor-preferences.json
std::optional<std::filesystem::path> editorPreferencesFilePath();

} // namespace ArtCade::EditorNative
