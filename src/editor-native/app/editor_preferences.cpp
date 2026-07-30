#include "editor-native/app/editor_preferences.h"

#include <nlohmann/json.hpp>

namespace ArtCade::EditorNative {

std::string editorAccentPresetToken(EditorAccentPreset preset) {
    switch (preset) {
        case EditorAccentPreset::ArtCadeBlue: return "artcadeblue";
        case EditorAccentPreset::Sage: return "sage";
        case EditorAccentPreset::SteelTeal: return "steelteal";
        case EditorAccentPreset::AmberOchre: return "amberochre";
        case EditorAccentPreset::Neutral: return "neutral";
    }
    return "artcadeblue";
}

EditorAccentPreset editorAccentPresetFromToken(const std::string& token) {
    if (token == "sage") return EditorAccentPreset::Sage;
    if (token == "steelteal") return EditorAccentPreset::SteelTeal;
    if (token == "amberochre") return EditorAccentPreset::AmberOchre;
    if (token == "neutral") return EditorAccentPreset::Neutral;
    return EditorAccentPreset::ArtCadeBlue;
}

bool EditorPreferences::fromJson(const std::string& text) {
    if (text.empty()) return false;
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(text);
    } catch (const nlohmann::json::exception&) {
        return false;
    }
    if (!root.is_object()) return false;

    EditorAccentPreset next = EditorAccentPreset::ArtCadeBlue;
    if (root.contains("accentPreset") && root["accentPreset"].is_string()) {
        next = editorAccentPresetFromToken(root["accentPreset"].get<std::string>());
    }
    accentPreset = next;
    return true;
}

std::string EditorPreferences::toJson() const {
    const nlohmann::json root = {
        {"version", 1},
        {"accentPreset", editorAccentPresetToken(accentPreset)},
    };
    return root.dump(2);
}

} // namespace ArtCade::EditorNative
