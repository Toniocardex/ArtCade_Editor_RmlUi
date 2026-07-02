#pragma once

#include "core/types.h"

namespace ArtCade::EditorNative {

// =============================================================================
// SelectionState — purely editorial focus, never authoring data.
//
// Multi-selection is deliberately out of scope for the spike (prompt §16): the
// editor tracks a single primary entity. Changing the selection is an intent,
// not a command, and never enters the undo stack.
// =============================================================================
struct SelectionState {
    EntityId primaryEntity = INVALID_ENTITY;

    bool hasEntity() const { return primaryEntity != INVALID_ENTITY; }
    void clear() { primaryEntity = INVALID_ENTITY; }
};

} // namespace ArtCade::EditorNative
