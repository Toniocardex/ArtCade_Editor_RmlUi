#pragma once

#include "editor-native/app/unsaved_guard.h"

namespace ArtCade::EditorNative {

// Modal native prompt shown before a destructive action when the project has
// unsaved changes. Returns Save / Discard / Cancel. Platform glue only — no
// project, coordinator or renderer knowledge.
UnsavedChoice confirmUnsavedChanges();

// Closing the Tileset Editor with an unapplied slicing config. Same
// Save/Discard/Cancel shape (Save = apply the pending slicing); the decision
// is resolved by the same resolveUnsavedGuard as the project-level guard.
UnsavedChoice confirmTilesetUnappliedChanges();

// Applying a re-slice that clears painted cells (their tile ids disappear
// from the new tile list). Counts only — this layer stays platform glue with
// no model knowledge. Returns true to proceed (apply and clear), false to
// abort; abort is the default and the only non-Windows answer.
bool confirmTilesetResliceImpact(int removedReferencedTiles, int orphanedCells,
                                 int affectedTilemaps);

} // namespace ArtCade::EditorNative
