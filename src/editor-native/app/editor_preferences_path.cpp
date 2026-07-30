#include "editor-native/app/editor_preferences_path.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#endif

#include <cstdlib>
#include <string>

namespace ArtCade::EditorNative {

std::optional<std::filesystem::path> editorPreferencesDirectory() {
#if defined(_WIN32)
    PWSTR folder = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT,
                                            nullptr, &folder);
    if (FAILED(hr) || !folder) {
        if (const char* env = std::getenv("LOCALAPPDATA")) {
            if (env[0] != '\0') return std::filesystem::path(env) / "ArtCade";
        }
        return std::nullopt;
    }
    std::filesystem::path root{folder};
    CoTaskMemFree(folder);
    return root / "ArtCade";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME")) {
        if (xdg[0] != '\0') return std::filesystem::path(xdg) / "ArtCade";
    }
    if (const char* home = std::getenv("HOME")) {
        if (home[0] != '\0') return std::filesystem::path(home) / ".config" / "ArtCade";
    }
    return std::nullopt;
#endif
}

std::optional<std::filesystem::path> recentProjectsPreferencesPath() {
    const auto dir = editorPreferencesDirectory();
    if (!dir) return std::nullopt;
    return *dir / "recent-projects.json";
}

std::optional<std::filesystem::path> editorPreferencesFilePath() {
    const auto dir = editorPreferencesDirectory();
    if (!dir) return std::nullopt;
    return *dir / "editor-preferences.json";
}

} // namespace ArtCade::EditorNative
