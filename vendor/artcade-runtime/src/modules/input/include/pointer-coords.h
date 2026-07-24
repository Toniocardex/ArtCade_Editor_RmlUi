#pragma once

#include "../../../core/types.h"

namespace ArtCade::Modules {

/** Canvas CSS selector used for Emscripten CSS→framebuffer scale (default #artcade-canvas). */
void pointerCoordsSetCanvasSelector(const char* selector);

/**
 * Map mouse coordinates to the canvas framebuffer pixel space used by the
 * Renderer and Raylib. On native builds this is identity. On Emscripten,
 * applies the same CSS/internal scale as the editor pick path.
 */
Vec2 pointerCoordsNormalizeToFramebuffer(float x, float y);

} // namespace ArtCade::Modules
