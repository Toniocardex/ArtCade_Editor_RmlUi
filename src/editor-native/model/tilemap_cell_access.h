#pragma once

#include "core/types.h"
#include "editor-native/model/tilemap_chunk_math.h"

namespace ArtCade::EditorNative {

// One cell's change, before -> after. Shared verbatim by the workspace-side
// PendingTileStroke accumulator and PaintTilemapCellsCommand's stored delta -
// defined exactly once here, not duplicated in editor_state.h or
// tilemap_commands.h.
struct TilemapCellChange {
    TilemapCellCoord cell;
    TilemapCell      before;
    TilemapCell      after;
};

// Reads a cell's current value, or nullopt if no chunk exists yet there.
// Shared by EditorCoordinator::apply() (workspace before-capture) and
// PaintTilemapCellsCommand (before-mismatch check) - one function, not
// duplicated logic in each caller.
TilemapCell readTilemapCell(const TilemapComponent& component, TilemapCellCoord cell);

// Writes one cell, lazily creating its chunk (chunkSize*chunkSize empty
// cells) if it doesn't exist yet - the only place a chunk is created during
// painting.
void writeTilemapCell(TilemapComponent& component, TilemapCellCoord cell, TilemapCell value);

// Removes every chunk that is now entirely empty (erasing a chunk's last
// non-empty cell removes the chunk, keeping the persisted structure sparse).
void pruneEmptyChunks(TilemapComponent& component);

} // namespace ArtCade::EditorNative
