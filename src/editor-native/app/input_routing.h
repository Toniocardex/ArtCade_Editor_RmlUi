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

struct GameplayKeyboardInputContext {
    bool sceneWorkspace         = true;
    bool viewportHasGameplayFocus = false;
    bool windowFocused          = true;
    bool rmlTextFieldFocused    = false;
    bool popupOpen              = false;
};

// Keyboard ownership is persistent gameplay focus, never pointer hover. The
// cursor may leave the Scene View without interrupting an active controller.
inline bool shouldForwardGameplayKeyboardInput(const GameplayKeyboardInputContext& ctx) {
    return ctx.sceneWorkspace
        && ctx.viewportHasGameplayFocus
        && ctx.windowFocused
        && !ctx.rmlTextFieldFocused
        && !ctx.popupOpen;
}

struct GameplayPointerInputContext {
    GameplayKeyboardInputContext keyboard;
    bool cursorInViewport = false;
};

// Pointer input remains spatial: it additionally requires the cursor to be
// over the actual viewport. This intentionally differs from keyboard routing.
inline bool shouldForwardGameplayPointerInput(const GameplayPointerInputContext& ctx) {
    return shouldForwardGameplayKeyboardInput(ctx.keyboard) && ctx.cursorInViewport;
}

} // namespace ArtCade::EditorNative
