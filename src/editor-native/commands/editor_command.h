#pragma once

#include "editor-native/commands/editor_operation_result.h"

namespace ArtCade::EditorNative {

class ProjectDocument;

// =============================================================================
// EditorCommand — a mutation of the authoring ProjectDocument that may enter
// the undo stack (prompt §4).
//
// Each command must: validate, apply the mutation, report a readable error on
// failure, declare the invalidation it produced, and provide an inverse for
// undo. Commands never touch presentation, the renderer, or SceneManager.
// =============================================================================
class EditorCommand {
public:
    virtual ~EditorCommand() = default;

    virtual EditorOperationResult apply(ProjectDocument& document) = 0;
    virtual EditorOperationResult undo(ProjectDocument& document)  = 0;

    virtual const char* name() const = 0;
};

} // namespace ArtCade::EditorNative
