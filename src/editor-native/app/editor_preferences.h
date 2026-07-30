#pragma once

#include <string>

namespace ArtCade::EditorNative {

// Built-in chrome accent presets (ADR-0027 role vocabulary). Closed set —
// adding user-defined/arbitrary colors would need revisiting this shape.
enum class EditorAccentPreset {
    ArtCadeBlue,
    Sage,
    SteelTeal,
    AmberOchre,
    Neutral,
};

// Lowercase token used for both the persisted JSON value and the RCSS class
// suffix ("accent-<token>"). ArtCadeBlue has no RCSS class (it's the base
// theme), but still round-trips through these helpers.
std::string editorAccentPresetToken(EditorAccentPreset preset);

// Unknown/empty token -> ArtCadeBlue. Never fails.
EditorAccentPreset editorAccentPresetFromToken(const std::string& token);

// App-local preference (ADR-0030 shape: no ProjectDocument, dirty, or Undo).
// Pure in-memory + JSON string roundtrip — no filesystem, no RmlUi.
struct EditorPreferences {
    EditorAccentPreset accentPreset = EditorAccentPreset::ArtCadeBlue;

    // Malformed / non-object root leaves *this unchanged and returns false.
    // An unrecognized accentPreset value inside otherwise-valid JSON falls
    // back to ArtCadeBlue rather than failing the parse.
    bool fromJson(const std::string& text);
    std::string toJson() const;
};

} // namespace ArtCade::EditorNative
