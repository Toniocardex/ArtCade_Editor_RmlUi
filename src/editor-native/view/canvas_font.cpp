#include "editor-native/view/canvas_font.h"

namespace ArtCade::EditorNative {

namespace {
// One atlas comfortably above every canvas text size (12-18 px), downscaled
// with bilinear filtering instead of loading one atlas per size.
constexpr int kAtlasSize = 32;
} // namespace

CanvasFont loadCanvasFont(const std::filesystem::path& resourceRoot) {
    CanvasFont canvasFont;
    const std::filesystem::path path =
        resourceRoot / "fonts" / "inter" / "Inter-Medium.ttf";
    if (!std::filesystem::exists(path)) return canvasFont;
    canvasFont.font = LoadFontEx(path.string().c_str(), kAtlasSize, nullptr, 0);
    canvasFont.loaded = canvasFont.font.texture.id != 0;
    if (canvasFont.loaded) {
        SetTextureFilter(canvasFont.font.texture, TEXTURE_FILTER_BILINEAR);
    }
    return canvasFont;
}

void unloadCanvasFont(CanvasFont& canvasFont) {
    if (canvasFont.loaded) UnloadFont(canvasFont.font);
    canvasFont = CanvasFont{};
}

void drawCanvasText(const Font& font, const std::string& text,
                    float x, float y, float size, Color color) {
    // Spacing 0: the TTF glyph advance already spaces the text; raylib's
    // extra per-glyph spacing is a bitmap-font artefact.
    DrawTextEx(font, text.c_str(), Vector2{x, y}, size, 0.f, color);
}

float measureCanvasText(const Font& font, const std::string& text, float size) {
    return MeasureTextEx(font, text.c_str(), size, 0.f).x;
}

void drawCanvasText(const CanvasFont& canvasFont, const std::string& text,
                    float x, float y, float size, Color color) {
    drawCanvasText(canvasFont.loaded ? canvasFont.font : GetFontDefault(),
                   text, x, y, size, color);
}

float measureCanvasText(const CanvasFont& canvasFont, const std::string& text,
                        float size) {
    return measureCanvasText(canvasFont.loaded ? canvasFont.font : GetFontDefault(),
                             text, size);
}

} // namespace ArtCade::EditorNative
