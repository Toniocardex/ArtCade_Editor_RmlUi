#pragma once

namespace ArtCade::Presentation {

/** Matches editor `EDITOR_ZOOM_MIN`. */
constexpr double kEditorZoomMin = 0.1;

/** Matches editor `EDITOR_ZOOM_MAX`. */
constexpr double kEditorZoomMax = 8.0;

/** Matches editor `EDITOR_CANVAS_PADDING_PX`. */
constexpr double kEditorCanvasPaddingPx = 16.0;

/** Matches editor `EDITOR_ZOOM_SNAP_DECIMALS`. */
constexpr int kEditorZoomSnapDecimals = 3;

/**
 * Clamps editor zoom into the legal range and snaps to fixed decimals.
 * @param zoom raw zoom factor
 */
double editor_zoom_clamp(double zoom);

} // namespace ArtCade::Presentation
