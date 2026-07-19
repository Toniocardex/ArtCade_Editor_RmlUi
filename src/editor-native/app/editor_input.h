#pragma once

namespace Rml { class Context; }

namespace ArtCade::EditorNative {

// Result of feeding one frame of raylib input into RmlUi — used to route the
// remainder to the viewport (prompt §19).
struct RmlInputResult {
    bool mouseOverUi = false;   // pointer is interacting with an RML element
    bool textFocus   = false;   // a text field currently has keyboard focus
};

// Input the application claims for itself this frame, withheld from RmlUi.
// Today: the wheel while Ctrl is held over the Tile Palette hole (palette
// zoom) — without this RmlUi would also scroll the Inspector. The claim must
// be dropped whenever its justifying rect is invalid or stale, so plain
// scrolling is never stolen from the panel.
struct RmlInputSuppression {
    bool mouseWheel = false;
};

// Translate this frame's raylib mouse/keyboard/text into the RmlUi context.
RmlInputResult pumpRmlInput(Rml::Context* context,
                            const RmlInputSuppression& suppression = {});

} // namespace ArtCade::EditorNative
