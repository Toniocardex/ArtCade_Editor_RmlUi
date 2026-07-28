#pragma once

#include <raylib.h>

#include <filesystem>
#include <string>

namespace ArtCade::EditorNative {

// Canvas-side text font (entity labels, scene chip, world readout, canvas
// messages): the same Inter face the RmlUi chrome uses, so raylib-drawn text
// stops reading as a different application than the panels around it.
//
// Ownership (AC-LIFE-001): created by the composition root after InitWindow,
// released before CloseWindow — the atlas is a GPU texture. Renderers receive
// a const reference; nothing else owns or mutates it. When loading fails the
// draw/measure helpers fall back to raylib's built-in font (text stays
// readable; the composition root logs the failure once).
struct CanvasFont {
    Font font{};
    bool loaded = false;
};

// Loads Inter-Medium from the editor resource root at atlas size 32 with
// bilinear filtering, so the 12-18 px canvas sizes downscale smoothly.
CanvasFont loadCanvasFont(const std::filesystem::path& resourceRoot);
void unloadCanvasFont(CanvasFont& canvasFont);

void drawCanvasText(const CanvasFont& canvasFont, const std::string& text,
                    float x, float y, float size, Color color);
float measureCanvasText(const CanvasFont& canvasFont, const std::string& text,
                        float size);

// ADR-0036: explicit-Font overloads so a Text component's own resolved font
// (EditorFontCache) shares the same draw/measure implementation as the
// default CanvasFont, rather than a second copy of the spacing/fallback
// logic. The two CanvasFont-taking overloads above are thin wrappers over
// these.
void drawCanvasText(const Font& font, const std::string& text,
                    float x, float y, float size, Color color);
float measureCanvasText(const Font& font, const std::string& text, float size);

} // namespace ArtCade::EditorNative
