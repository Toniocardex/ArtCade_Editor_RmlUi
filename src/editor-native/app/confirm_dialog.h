#pragma once

#include "editor-native/app/unsaved_guard.h"

#include <cstddef>
#include <string>

namespace ArtCade::EditorNative {

// Modal native prompt shown before a destructive action when the project has
// unsaved changes. Returns Save / Discard / Cancel. Platform glue only — no
// project, coordinator or renderer knowledge.
UnsavedChoice confirmUnsavedChanges();
UnsavedChoice confirmUnsavedChanges(const std::string& detail);

// Closing the Tileset Editor with an unapplied slicing config. Same
// Save/Discard/Cancel shape (Save = apply the pending slicing); the decision
// is resolved by the same resolveUnsavedGuard as the project-level guard.
UnsavedChoice confirmTilesetUnappliedChanges();

// Permanent Script Asset deletion including the confined .lua source file.
// Yes = proceed; No/Cancel/dismiss = abort. Default is No (safe). Non-Windows
// always returns false.
bool confirmDeleteScriptAsset(const std::string& name,
                              const std::string& relativeSourcePath);

// Applying a re-slice that clears painted cells (their tile ids disappear
// from the new tile list). Counts only — this layer stays platform glue with
// no model knowledge. Returns true to proceed (apply and clear), false to
// abort; abort is the default and the only non-Windows answer.
bool confirmTilesetResliceImpact(int removedReferencedTiles, int orphanedCells,
                                 int affectedTilemaps);

/** ADR-0013 Slice 3 — remove gameplay component that Logic still references. */
enum class ComponentLogicRemoveChoice {
    Cancel,
    RemoveAndKeepLogic,
    ReviewReferences,
};

ComponentLogicRemoveChoice confirmRemoveComponentWithLogicRefs(
    const std::string& componentDisplayName,
    std::size_t actionCount,
    std::size_t conditionCount,
    std::size_t triggerCount);

} // namespace ArtCade::EditorNative
