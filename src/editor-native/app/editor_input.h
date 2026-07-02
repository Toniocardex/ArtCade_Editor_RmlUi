#pragma once

namespace Rml { class Context; }

namespace ArtCade::EditorNative {

// Result of feeding one frame of raylib input into RmlUi — used to route the
// remainder to the viewport (prompt §19).
struct RmlInputResult {
    bool mouseOverUi = false;   // pointer is interacting with an RML element
    bool textFocus   = false;   // a text field currently has keyboard focus
};

// Translate this frame's raylib mouse/keyboard/text into the RmlUi context.
RmlInputResult pumpRmlInput(Rml::Context* context);

} // namespace ArtCade::EditorNative
