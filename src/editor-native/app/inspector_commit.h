#pragma once

#include "core/types.h"
#include "editor-native/commands/editor_operation_result.h"

#include <optional>
#include <string>

namespace ArtCade::EditorNative {

class EditorCoordinator;

// =============================================================================
// Inspector Transform commit — single path for Position / Rotation / Scale.
//
//   NumberField (Position | Rotation | Scale)
//     → commitInspectorTransformField()
//       → SetEntityTransformCommand
//         → ProjectDocument::patchInstanceTransform
//           → EditorInvalidation::Inspector | Viewport
//
// Degrees are converted to radians only at this boundary. The document stores
// rotation in radians exclusively.
// =============================================================================

enum class InspectorTransformField {
    PositionX,
    PositionY,
    RotationDegrees,
    ScaleX,
    ScaleY,
};

/** Parse a number field. Returns nullopt for empty/garbage/overflow input. */
std::optional<float> parseNumberField(const std::string& text);

EditorOperationResult commitInspectorTransformField(EditorCoordinator& coordinator,
                                                    EntityId entityId,
                                                    InspectorTransformField field,
                                                    const std::string& text);

} // namespace ArtCade::EditorNative
