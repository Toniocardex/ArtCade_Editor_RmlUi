#pragma once

#include "core/types.h"
#include "editor-native/model/tilemap_cell_access.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace ArtCade::EditorNative {

// Shared cap for any single tilemap region operation (Rectangle Solid,
// Rectangle Outline, Flood Fill): bounds the delta's memory, the Undo/Redo
// entry size and the editor's per-click responsiveness. Deliberately not
// derived from kMaxTilemapChunkSize (tilemap_validation.h) - that bounds a
// persisted chunk's cell count, a different concern from "how many cells one
// editorial operation may touch at once". They happen to share a value today.
constexpr std::size_t kMaxTilePaintOperationCells = 65536;

// Cell-neighbor offset, overflow-checked: returns nullopt instead of wrapping
// past INT_MIN/INT_MAX. floodFillChanges uses this to treat the int32
// coordinate range's edge as a hard boundary rather than undefined behaviour.
std::optional<TilemapCellCoord> tryOffsetCell(TilemapCellCoord cell, int dx, int dy);

// Result shape shared by every region builder below - mirrors
// validateTilemapComponent's own std::optional<std::string> error convention
// rather than introducing a new generic Result<T, E> type with no other user
// in this codebase. `error` set means `changes` is always empty: the whole
// operation is rejected, never partially built.
struct TileRegionBuildResult {
    std::vector<TilemapCellChange> changes;
    std::optional<std::string>     error;
};

// Per-destination-cell decision of a region operation. Three explicit states
// so a stamp hole ("touch nothing here") can never be confused with an erase
// ("clear this cell") - the two ambiguities a bare optional<TilemapCell>
// would collapse at the wrong level.
struct TileReplacementDecision {
    enum class Kind { Skip, Erase, Paint };

    Kind   kind = Kind::Skip;
    TileId tileId;   // meaningful only for Paint

    static TileReplacementDecision skip()  { return {}; }
    static TileReplacementDecision erase() { return {Kind::Erase, {}}; }
    static TileReplacementDecision paint(TileId id) { return {Kind::Paint, std::move(id)}; }
};

// Called once per candidate cell by the provider-based builders below. Paint
// always stamps TileTransformFlags::None (per-cell transforms are a future
// feature; nothing authors them yet).
using TileReplacementProvider = std::function<TileReplacementDecision(TilemapCellCoord)>;

// Rectangle Solid: every cell in the axis-aligned box spanned by the two
// corners (inclusive, order-independent - a drag toward the origin works the
// same as one away from it). Cells already equal to `replacement` are
// omitted from the result (mirrors normalizePaintStrokeChanges' before==after
// filtering), so a rectangle drawn over an already-uniform region can
// legitimately produce an empty, error-free result. Width/height are widened
// to int64_t before any arithmetic and checked against
// kMaxTilePaintOperationCells before the area is ever multiplied out or
// iterated, so neither the size check nor the generation loop can overflow -
// not even for corners near INT_MIN/INT_MAX.
TileRegionBuildResult rectangleFillChanges(const TilemapComponent& component,
                                           TilemapCellCoord corner1, TilemapCellCoord corner2,
                                           TilemapCell replacement);

// Provider variant (multi-tile stamps): the area cap is checked before any
// provider call; Skip cells are omitted from the delta; before == after cells
// are filtered exactly like the single-replacement variant.
TileRegionBuildResult rectangleFillChanges(const TilemapComponent& component,
                                           TilemapCellCoord corner1, TilemapCellCoord corner2,
                                           const TileReplacementProvider& provider);

// Rectangle Outline: only the border cells, generated directly from the four
// edges in O(perimeter) - it never iterates the interior. A 1-wide or
// 1-tall rectangle has no interior, so its "outline" is the full strip
// (every cell), matching what a human would expect to see, not an empty
// result.
TileRegionBuildResult rectangleOutlineChanges(const TilemapComponent& component,
                                              TilemapCellCoord corner1, TilemapCellCoord corner2,
                                              TilemapCell replacement);
TileRegionBuildResult rectangleOutlineChanges(const TilemapComponent& component,
                                              TilemapCellCoord corner1, TilemapCellCoord corner2,
                                              const TileReplacementProvider& provider);

// Flood Fill: the 4-connected region starting at `origin` whose cells all
// currently equal readTilemapCell(component, origin), replaced with
// `replacement`. Returns an empty, error-free result immediately - no
// traversal at all - when the target already equals `replacement`. The
// sparse grid has no bounds, so an open (unenclosed) empty region is stopped
// only by kMaxTilePaintOperationCells; that case reports `error` with
// `changes` always empty (the whole fill is cancelled, never partially
// applied), since there is no well-defined boundary to fill up to.
TileRegionBuildResult floodFillChanges(const TilemapComponent& component,
                                       TilemapCellCoord origin, TilemapCell replacement);

// Provider variant. The traversed region is still defined purely by "cells
// currently equal to the origin's value": Skip decisions leave a cell
// unchanged but never interrupt the traversal, and the operation cap counts
// VISITED cells (not emitted changes), so a huge region full of Skips is
// rejected exactly like a huge uniform fill. There is no target==replacement
// early-out here - with a pattern there is no single replacement; a provider
// that happens to repaint the origin's own value simply yields a smaller
// delta (the single-replacement variant keeps its early-out and delegates).
TileRegionBuildResult floodFillChanges(const TilemapComponent& component,
                                       TilemapCellCoord origin,
                                       const TileReplacementProvider& provider);

} // namespace ArtCade::EditorNative
