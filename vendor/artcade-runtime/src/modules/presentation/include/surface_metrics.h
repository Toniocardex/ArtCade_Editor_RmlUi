#pragma once

#include "presentation_types.h"

namespace ArtCade::Presentation {

/**
 * Derives framebuffer size from CSS canvas size and device pixel ratio.
 * framebuffer = round(css * dpr) per axis.
 * @param cssW CSS width in pixels (must be >= 0)
 * @param cssH CSS height in pixels (must be >= 0)
 * @param devicePixelRatio ratio >= 0; values <= 0 clamp to 1
 */
SurfaceMetrics surface_metrics_from_css(double cssW, double cssH,
                                      double devicePixelRatio);

/**
 * Maps a CSS pointer position to framebuffer surface pixels.
 * Uses independent X/Y scale derived from rounded framebuffer vs CSS size.
 */
SurfacePoint surface_point_from_css(CssPoint css, const SurfaceMetrics& metrics);

} // namespace ArtCade::Presentation
