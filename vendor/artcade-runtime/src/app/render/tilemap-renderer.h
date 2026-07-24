#pragma once
// =============================================================================
// tilemap-renderer  —  scene tilemap rendering (atlas + palette fallback)
// =============================================================================
//
// Extracted from app.cpp::renderActiveScene() during the post-phase-6 split.
// Walks per-layer tilemap grids back→front (SceneDef.layers: index 0 =
// background drawn first, last = foreground), falling back to merged tilemap.
//
//   1. If the layer references a tileset and the tileset is found in the
//      live SceneManager list (or the startup fallback cache), draw the
//      tile as a spritesheet region via Renderer::drawSpriteRegion.
//   2. Otherwise, draw a solid rect using the per-id colour from the
//      project's tile palette.
//   3. Otherwise, a neutral grey rect (so missing/invalid tile ids are
//      still visible in the editor instead of silently disappearing).
//
// Ownership: the renderer owns no caches. `Application` keeps the
// `tileColors_` / `tilesets_` maps that `loadProject()` populates and
// passes them by reference each frame. This keeps the renderer
// stateless and trivially relocatable.
// =============================================================================

#include "../../core/types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ArtCade {

namespace Modules { class Renderer; }

namespace TilemapRenderer {

/**
 * Draw scene tilemaps into the active renderer frame.
 *
 * When @p layerStack and @p tilemapLayers are present, each layer is drawn
 * bottom→top (index 0 in @p layerStack is painted last). Otherwise draws the
 * merged @p mergedTilemap grid (legacy / single-layer projects).
 *
 * Skips id == 0 cells (empty). Spritesheet region preferred over palette colour.
 */
void draw(Modules::Renderer& renderer,
          const TilemapData* mergedTilemap,
          const std::unordered_map<std::string, TilemapData>* tilemapLayers,
          const std::unordered_map<std::string, SceneLayerSettings>& layerSettings,
          const std::vector<SceneLayerDef>& layerStack,
          const std::vector<TilesetAsset>& liveTilesets,
          const std::unordered_map<std::string, TilesetAsset>& startupCache,
          const std::unordered_map<int, Vec4>& palette);

} // namespace TilemapRenderer
} // namespace ArtCade
