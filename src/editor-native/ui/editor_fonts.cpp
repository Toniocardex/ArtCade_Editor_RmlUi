#include "editor-native/ui/editor_fonts.h"

#include <RmlUi/Core.h>

#include <array>

namespace ArtCade::EditorNative {
namespace {

struct FontFace {
    const char* relativePath;
    const char* label;
    Rml::Style::FontWeight weight;
    bool fallback;
};

// Inter — guaranteed Latin coverage. Required; the Regular face is registered as
// the global fallback so any missing glyph (or a missing primary family) still
// renders instead of disappearing.
constexpr std::array<FontFace, 4> kRequiredFonts{{
    {"fonts/inter/Inter-Regular.ttf",  "Inter Regular",  Rml::Style::FontWeight::Normal, true},
    {"fonts/inter/Inter-Medium.ttf",   "Inter Medium",   static_cast<Rml::Style::FontWeight>(500), false},
    {"fonts/inter/Inter-SemiBold.ttf", "Inter SemiBold", static_cast<Rml::Style::FontWeight>(600), false},
    {"fonts/inter/Inter-Bold.ttf",     "Inter Bold",     Rml::Style::FontWeight::Bold, false},
}};

// JetBrains Mono — primary UI family (matches the ArtCade style guide). Optional:
// if a face is missing or unreadable the UI falls back to Inter rather than
// failing to start. Static .ttf only — this FreeType build has zlib/brotli off
// and cannot decode .woff/.woff2.
constexpr std::array<FontFace, 5> kOptionalFonts{{
    {"fonts/jetbrains-mono/JetBrainsMono-Regular.ttf",  "JetBrains Mono Regular",  Rml::Style::FontWeight::Normal, false},
    {"fonts/jetbrains-mono/JetBrainsMono-Medium.ttf",   "JetBrains Mono Medium",   static_cast<Rml::Style::FontWeight>(500), false},
    {"fonts/jetbrains-mono/JetBrainsMono-SemiBold.ttf", "JetBrains Mono SemiBold", static_cast<Rml::Style::FontWeight>(600), false},
    {"fonts/jetbrains-mono/JetBrainsMono-Bold.ttf",     "JetBrains Mono Bold",     Rml::Style::FontWeight::Bold, false},
    // Tabler Icons (MIT) — icon glyphs in the Private Use Area, family "tabler-icons".
    {"fonts/tabler/tabler-icons.ttf",                   "Tabler Icons",            Rml::Style::FontWeight::Normal, false},
}};

} // namespace

FontLoadResult loadEditorFonts(const std::filesystem::path& resourceRoot) {
    for (const FontFace& font : kRequiredFonts) {
        const std::filesystem::path path = resourceRoot / font.relativePath;
        if (!std::filesystem::exists(path)) {
            return {
                false,
                std::string("Missing required font: ") + font.label +
                    " (" + path.string() + ")"
            };
        }

        if (!Rml::LoadFontFace(path.string(), font.fallback, font.weight)) {
            return {
                false,
                std::string("RmlUi failed to load font: ") + font.label +
                    " (" + path.string() + ")"
            };
        }
    }

    for (const FontFace& font : kOptionalFonts) {
        const std::filesystem::path path = resourceRoot / font.relativePath;
        if (!std::filesystem::exists(path)) {
            Rml::Log::Message(Rml::Log::LT_WARNING,
                "[editor] optional font missing, falling back to Inter: %s",
                path.string().c_str());
            continue;
        }
        if (!Rml::LoadFontFace(path.string(), font.fallback, font.weight)) {
            Rml::Log::Message(Rml::Log::LT_WARNING,
                "[editor] optional font failed to load, falling back to Inter: %s",
                path.string().c_str());
        }
    }

    return {true, {}};
}

} // namespace ArtCade::EditorNative
