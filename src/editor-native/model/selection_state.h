#pragma once

#include "core/types.h"

#include <optional>
#include <utility>

namespace ArtCade::EditorNative {

// =============================================================================
// SelectionState — purely editorial focus, never authoring data.
//
// Multi-selection is deliberately out of scope for the spike (prompt §16): the
// editor tracks either one primary entity or one Object Type. Changing the
// selection is an intent, not a command, and never enters the undo stack.
// =============================================================================
struct SelectionState {
    EntityId primaryEntity = INVALID_ENTITY;
    std::optional<ObjectTypeId> selectedObjectTypeId;

    bool hasEntity() const { return primaryEntity != INVALID_ENTITY; }
    bool hasObjectType() const { return selectedObjectTypeId.has_value(); }

    void selectEntity(EntityId id) {
        primaryEntity = id;
        selectedObjectTypeId.reset();
    }
    void selectObjectType(ObjectTypeId id) {
        primaryEntity = INVALID_ENTITY;
        selectedObjectTypeId = std::move(id);
    }
    void clear() {
        primaryEntity = INVALID_ENTITY;
        selectedObjectTypeId.reset();
    }
};

} // namespace ArtCade::EditorNative
