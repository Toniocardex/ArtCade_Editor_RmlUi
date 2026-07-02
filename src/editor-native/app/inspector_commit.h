#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_operation_result.h"

#include <optional>
#include <string>

namespace ArtCade::EditorNative {

class EditorCoordinator;

// =============================================================================
// Inspector commit logic — the reference path of the whole spike (prompt §30),
// kept UI-free so it is unit-testable without RmlUi.
//
//   NumberField Position X
//     → commitInspectorPositionX()
//       → SetEntityPositionCommand
//         → ProjectDocument
//           → EditorInvalidation::Inspector | Viewport
//
// The RmlUi InspectorController is a one-line shim that forwards the committed
// text here. Parsing failures never reach a command, so the document is never
// touched by invalid input (prompt §24.13).
// =============================================================================

/** Parse a number field. Returns nullopt for empty/garbage/overflow input. */
std::optional<float> parseNumberField(const std::string& text);

/**
 * Commit a new X for @p entityId's position, keeping its current Y. On a parse
 * failure no command runs and the document is unchanged; the error is logged.
 */
EditorOperationResult commitInspectorPositionX(EditorCoordinator& coordinator,
                                               EntityId           entityId,
                                               const std::string& text);

/** As commitInspectorPositionX, for the Y axis (keeps the current X). */
EditorOperationResult commitInspectorPositionY(EditorCoordinator& coordinator,
                                               EntityId           entityId,
                                               const std::string& text);

} // namespace ArtCade::EditorNative
