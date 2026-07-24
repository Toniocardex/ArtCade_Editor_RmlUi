#pragma once
// =============================================================================
// editor-input-controller  -- native canvas mouse input
// =============================================================================
//
// Extracted from editor-api.cpp during the Phase 5 split. Owns:
//   - the canvas selector (set at init() time);
//   - the on-screen / on-world coordinate mapping;
//   - the mouse/key callbacks plumbed into emscripten;
//   - pickEntityAt helper.
//
//
// editor-api.cpp itself is now only static state, lifecycle, wiring,
// notifications, and the EMSCRIPTEN_KEEPALIVE exports.
//
// WASM-only.
// =============================================================================

#ifdef __EMSCRIPTEN__

namespace ArtCade::EditorInputController {

/**
 * Capture the canvas CSS selector and register native mouse/keyboard
 * callbacks. Replaces the inline body of EditorAPI::init() for the input
 * part — EditorAPI::init() still owns the high-level lifecycle and
 * console-line logging.
 */
void initCanvas(const char* canvasSelector);

/** Detach every native callback. Mirrors EditorAPI::shutdown(). */
void shutdownCanvas();

} // namespace ArtCade::EditorInputController

#endif // __EMSCRIPTEN__
