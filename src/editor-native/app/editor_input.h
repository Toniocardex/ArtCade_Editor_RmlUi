#pragma once

namespace Rml { class Context; }

namespace ArtCade::EditorNative {

struct KeyboardFrameSnapshot;
struct RmlInputSuppression;

// Result of feeding one frame of raylib input into RmlUi — used to route the
// remainder to the viewport (prompt §19).
struct RmlInputResult {
    bool mouseOverUi = false;   // pointer is interacting with an RML element
    bool textFocus   = false;   // a text field currently has keyboard focus
};

// Input the application claims for itself this frame, withheld from RmlUi.
// Today: the wheel while the pointer is over the Tile Palette hole (scroll /
// zoom) — without this RmlUi would also scroll dock chrome. The claim must
// be dropped whenever its justifying rect is invalid or stale.
struct RmlInputSuppression {
    bool mouseWheel = false;
};

// Translate this frame's mouse (Raylib) + keyboard snapshot into the RmlUi
// context. Keyboard must not be re-read from Raylib here (ADR-0024).
RmlInputResult pumpRmlInput(Rml::Context* context,
                            const KeyboardFrameSnapshot& keyboard,
                            const RmlInputSuppression& suppression = {});

} // namespace ArtCade::EditorNative
