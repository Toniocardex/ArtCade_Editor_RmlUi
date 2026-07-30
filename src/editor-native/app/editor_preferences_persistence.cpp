#include "editor-native/app/editor_preferences_persistence.h"

#include "editor-native/app/editor_preferences_path.h"

#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace ArtCade::EditorNative {

namespace {

std::string readFileText(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "could not open preferences file";
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!in.good() && !in.eof()) {
        error = "could not read preferences file";
        return {};
    }
    return ss.str();
}

bool writeFileAtomic(const std::filesystem::path& path, const std::string& text,
                     std::string& error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        error = "could not create preferences directory: " + ec.message();
        return false;
    }
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "could not open temporary preferences file";
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!out.good()) {
            error = "could not write temporary preferences file";
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(tmp, path, ec);
        if (ec) {
            error = "could not replace preferences file: " + ec.message();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    return true;
}

} // namespace

EditorPreferencesPersistResult loadEditorPreferences(EditorPreferences& prefs) {
    EditorPreferencesPersistResult out;
    const auto path = editorPreferencesFilePath();
    if (!path) {
        prefs = EditorPreferences{};
        out.ok = true;
        out.message = "preferences directory unavailable";
        return out;
    }
    std::error_code ec;
    if (!std::filesystem::exists(*path, ec) || ec) {
        prefs = EditorPreferences{};
        out.ok = true;
        return out;
    }
    std::string error;
    const std::string text = readFileText(*path, error);
    if (!error.empty() && text.empty()) {
        out.message = error;
        return out;
    }
    if (!prefs.fromJson(text)) {
        prefs = EditorPreferences{};
        out.message = "malformed editor preferences";
        return out;
    }
    out.ok = true;
    return out;
}

EditorPreferencesPersistResult saveEditorPreferences(const EditorPreferences& prefs) {
    EditorPreferencesPersistResult out;
    const auto path = editorPreferencesFilePath();
    if (!path) {
        out.message = "preferences directory unavailable";
        return out;
    }
    std::string error;
    if (!writeFileAtomic(*path, prefs.toJson(), error)) {
        out.message = std::move(error);
        return out;
    }
    out.ok = true;
    return out;
}

} // namespace ArtCade::EditorNative
