#pragma once

#include "core/types.h"

#include <cmath>
#include <string>

namespace ArtCade::EditorNative {

struct SceneViewportPreset {
    const char* id = nullptr;
    const char* label = nullptr;
    Vec2 size{};
};

inline constexpr SceneViewportPreset kSceneViewportPresets[] = {
    {"artcade-classic", "ArtCade Classic", {512.f, 320.f}},
    {"16-9-small", "16:9 Small", {640.f, 360.f}},
    {"16-9-medium", "16:9 Medium", {960.f, 540.f}},
    {"hd", "HD", {1280.f, 720.f}},
};

inline bool matchesWholePixelSize(Vec2 actual, Vec2 preset) {
    constexpr float epsilon = 0.001f;
    return std::fabs(actual.x - preset.x) <= epsilon
        && std::fabs(actual.y - preset.y) <= epsilon;
}

inline const SceneViewportPreset* findSceneViewportPreset(Vec2 size) {
    for (const SceneViewportPreset& preset : kSceneViewportPresets) {
        if (matchesWholePixelSize(size, preset.size)) return &preset;
    }
    return nullptr;
}

// ASCII "x" (not U+00D7): Inter does not reliably glyph the multiply sign, so
// "512 × 320" rendered as "512  320" in the Game View preset dropdown.
inline std::string sceneViewportSizeText(Vec2 size) {
    return std::to_string(static_cast<int>(std::lround(size.x))) + "x"
         + std::to_string(static_cast<int>(std::lround(size.y)));
}

/** Display label including dimensions, e.g. "ArtCade Classic - 512x320" or Custom. */
inline std::string sceneViewportPresetLabel(Vec2 size) {
    if (const SceneViewportPreset* preset = findSceneViewportPreset(size)) {
        return std::string(preset->label) + " - " + sceneViewportSizeText(size);
    }
    return std::string("Custom - ") + sceneViewportSizeText(size);
}

} // namespace ArtCade::EditorNative
