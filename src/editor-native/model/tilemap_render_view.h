#pragma once

#include "core/types.h"
#include "editor-native/model/scene_frame_snapshot.h"

#include <vector>

namespace ArtCade::EditorNative {

// Resolves every populated cell of a TilemapComponent into world-space
// destination rects (cell (0,0) at originPosition, growing by cellSize per
// step - the owning instance's Transform.position IS originPosition; the
// component has no origin field of its own, see ADR-0001) plus the tileset
// pixel source rect for each cell's tile id. Returns SceneFrameTilemapCell
// directly (not a second, identically-shaped type) since the snapshot embeds
// this result verbatim. A cell whose tileId does not resolve in
// tileset.tiles is skipped (validateTilemapComponent is what should have
// rejected that state; this is a defensive skip, not a silent substitute for
// validation).
std::vector<SceneFrameTilemapCell> tilemapRenderCells(
    const TilemapComponent& tilemap, const TilesetAsset& tileset, Vec2 originPosition);

} // namespace ArtCade::EditorNative
