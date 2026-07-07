#pragma once

#include "core/types.h"
#include "editor-native/model/scene_frame_snapshot.h"

#include <optional>
#include <vector>

namespace ArtCade::EditorNative {

// World-space destination rect of one tilemap cell: cell (0,0) sits at
// `origin`, growing by cellSize per step. The one shared formula behind both
// Edit's tilemapRenderCells (recomputed every frame from the live authoring
// TilemapComponent) and Play's per-frame snapshot (recomputed every frame
// from the runtime entity's current Transform.position, which a mover can
// change) - so a tilemap always follows its owning entity's authoritative
// position in either mode, never a value baked in once at Play start.
SceneFrameRect tilemapCellDestination(Vec2 origin, Vec2 cellSize, int cellX, int cellY);

// Resolves every populated cell of a TilemapComponent into world-space
// destination rects (cell (0,0) at originPosition, growing by cellSize per
// step - the owning instance's Transform.position IS originPosition; the
// component has no origin field of its own, see ADR-0001) plus the tileset
// pixel source rect for each cell's tile id. Returns SceneFrameTilemapCell
// directly (not a second, identically-shaped type) since the snapshot embeds
// this result verbatim. A cell whose tileId does not resolve in
// tileset.tiles is skipped (validateTilemapComponent is what should have
// rejected that state; this is a defensive skip, not a silent substitute for
// validation) - lenient by design, since Edit renders every frame and must
// never refuse to draw the rest of a scene over one bad cell.
std::vector<SceneFrameTilemapCell> tilemapRenderCells(
    const TilemapComponent& tilemap, const TilesetAsset& tileset, Vec2 originPosition);

// One populated cell resolved against a tileset, in *local* cell coordinates -
// never world-space, since the caller may not have a final world position yet
// (or may need to recompute it every frame from one that changes, e.g. a
// moving runtime entity in Play).
struct TilemapResolvedCell {
    int cellX = 0;
    int cellY = 0;
    SceneFrameRect source;   // tileset pixel source rect
};

// Same chunk/cell traversal as tilemapRenderCells, but strict: an unresolvable
// tile id fails the whole call (nullopt) instead of being skipped.
// PlaySession::materialize is a one-shot gate that must reject Play atomically
// rather than silently start with content the author placed quietly missing -
// the opposite contract from tilemapRenderCells' own lenient skip, which is
// why this is a separate function rather than a flag on that one.
std::optional<std::vector<TilemapResolvedCell>> resolveTilemapCellsStrict(
    const TilemapComponent& tilemap, const TilesetAsset& tileset);

} // namespace ArtCade::EditorNative
