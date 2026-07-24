#pragma once
// =============================================================================
// parallax-renderer  —  scrolling repeating background images per render layer
// =============================================================================
//
// Each SceneLayerDef may carry an optional repeating background image plus a
// parallax factor. This walks the layer stack back-to-front and paints those
// backgrounds into the active world pass, BEFORE the tilemap and entities, so
// they sit behind everything.
//
// Parallax under the single world Camera2D: the camera maps a world point W to
// screen (W - cameraTopLeft). To make a background scroll at factor f we draw
// its content offset so the texel shown at the viewport edge advances by
// f·cameraTopLeft (plus a constant auto-scroll). f<1 → slower (far), f>1 →
// faster (near), f=1 → locked to the world like normal geometry.
//
// Stateless: auto-scroll phase comes from the caller's elapsed game-time.
// =============================================================================

#include "../../core/types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade {

namespace Modules { class Renderer; }

namespace ParallaxRenderer {

/**
 * Paint every layer's repeating background image into the active world pass.
 *
 * @param renderer      active renderer (inside beginFrame/endWorldPass)
 * @param layerStack    scene layers (index 0 = background / drawn first)
 * @param layerSettings active scene's per-layer visual overrides (keyed by id)
 * @param cameraTopLeft world point shown at the viewport top-left
 * @param viewSize      visible world size (width/height in world units)
 * @param elapsed       game-time seconds, drives the auto-scroll phase
 */
void draw(Modules::Renderer& renderer,
          const std::vector<SceneLayerDef>& layerStack,
          const std::unordered_map<std::string, SceneLayerSettings>& layerSettings,
          const Vec2& cameraTopLeft,
          const Vec2& viewSize,
          float elapsed);

} // namespace ParallaxRenderer
} // namespace ArtCade
