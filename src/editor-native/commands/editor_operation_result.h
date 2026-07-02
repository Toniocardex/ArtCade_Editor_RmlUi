#pragma once

#include "editor-native/commands/domain_change.h"
#include "editor-native/commands/editor_invalidation.h"

#include <string>

namespace ArtCade::EditorNative {

// =============================================================================
// EditorOperationResult — the single return shape of every command and intent.
//
//   ok           did the operation succeed?
//   change       what changed in the authoring domain (for projection/runtime)
//   invalidation which panels must refresh (None on failure)
//   error        human-readable message for the console on failure
//
// Failure is explicit and side-effect free: a failed operation neither mutates
// state nor produces invalidation (prompt §24.2, §24.3).
// =============================================================================
struct EditorOperationResult {
    bool                ok           = false;
    DomainChange        change;
    EditorInvalidation  invalidation = EditorInvalidation::None;
    std::string         error;

    static EditorOperationResult success(EditorInvalidation inv,
                                         DomainChange change = DomainChange::none()) {
        return EditorOperationResult{true, std::move(change), inv, {}};
    }
    static EditorOperationResult failure(std::string message) {
        return EditorOperationResult{false, DomainChange::none(),
                                     EditorInvalidation::None, std::move(message)};
    }
};

} // namespace ArtCade::EditorNative
