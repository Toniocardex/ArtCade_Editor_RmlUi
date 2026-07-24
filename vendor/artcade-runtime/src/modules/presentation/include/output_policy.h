#pragma once

#include "presentation_types.h"

namespace ArtCade::Presentation {

/**
 * Computes framebuffer placement for a logical viewport under an output policy.
 * @param surfaceW framebuffer width in pixels (must be > 0; invalid values clamp)
 * @param surfaceH framebuffer height in pixels (must be > 0; invalid values clamp)
 * @param logicalW game viewport width (must be > 0; invalid values clamp)
 * @param logicalH game viewport height (must be > 0; invalid values clamp)
 */
OutputPlacement output_placement_compute(double surfaceW, double surfaceH,
                                       double logicalW, double logicalH,
                                       OutputPolicy policy);

} // namespace ArtCade::Presentation
