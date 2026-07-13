#pragma once

namespace ArtCade::EditorNative {

// =============================================================================
// Input routing — one pipeline, expressed as a pure predicate (prompt §19).
//
//   platform event → RmlUi → consumed? stop
//                          → else viewport (only under the conditions below)
//                          → else PlaySession
//
// The viewport receives input only when the cursor is inside it, RmlUi did not
// consume the event, no RML text field has focus, and no popup is open. Keeping
// this a free function lets the rule be unit-tested without a window or RmlUi
// (prompt §24.16).
// =============================================================================
struct ViewportInputContext {
    bool cursorInViewport    = false;
    bool rmlConsumedEvent    = false;
    bool rmlTextFieldFocused = false;
    bool rmlPopupOpen        = false;
};

inline bool shouldViewportReceiveInput(const ViewportInputContext& ctx) {
    return ctx.cursorInViewport
        && !ctx.rmlConsumedEvent
        && !ctx.rmlTextFieldFocused
        && !ctx.rmlPopupOpen;
}

struct GameplayInputContext {
    bool sceneWorkspace      = true;
    bool cursorInViewport    = false;
    bool rmlTextFieldFocused = false;
};

// Runtime simulation may continue while another workspace is visible, but
// gameplay controls are forwarded only from the focused Scene surface.
inline bool shouldForwardGameplayInput(const GameplayInputContext& ctx) {
    return ctx.sceneWorkspace && ctx.cursorInViewport && !ctx.rmlTextFieldFocused;
}

} // namespace ArtCade::EditorNative
