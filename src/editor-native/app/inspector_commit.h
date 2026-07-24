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

// Scene / Inspector color helpers (RGB hex; alpha stays a separate 0–1 field).
struct ColorRgb {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;
};

/** Format RGB of @p color as uppercase `#RRGGBB` (alpha ignored). */
std::string formatColorHexRgb(const Vec4& color);

/**
 * Parse `#RGB` or `#RRGGBB` (optional leading `#`, case-insensitive) into
 * channels in [0,1]. Empty / wrong length / non-hex → nullopt.
 */
std::optional<ColorRgb> parseColorHexRgb(const std::string& text);

/** True while the buffer is a plausible incomplete hex color edit. */
bool incompleteColorHexBuffer(const std::string& text);

/** Format document alpha [0,1] as an integer percent string (e.g. "100"). */
std::string formatOpacityPercent(float alpha);

/**
 * Parse a percent buffer ("100", "100%", "50.5") into document alpha [0,1].
 * Out of range is clamped after a successful numeric parse.
 */
std::optional<float> parseOpacityPercent(const std::string& text);

/** CSS `rgba(r,g,b,a)` for swatch fills (channels from document Vec4). */
std::string formatColorCssRgba(const Vec4& color);

enum class OpacitySliderChangeDisposition {
    PreviewOnly,
    CommitImmediately,
};

/** dragActive → PreviewOnly; click/keyboard change without drag → CommitImmediately. */
OpacitySliderChangeDisposition classifyOpacitySliderChange(bool dragActive);

/** Component-wise equality for background / draft reconciliation. */
bool sameSceneBackgroundColor(const Vec4& a, const Vec4& b);

EditorOperationResult commitInspectorTransformField(EditorCoordinator& coordinator,
                                                    EntityId entityId,
                                                    InspectorTransformField field,
                                                    const std::string& text);

} // namespace ArtCade::EditorNative
