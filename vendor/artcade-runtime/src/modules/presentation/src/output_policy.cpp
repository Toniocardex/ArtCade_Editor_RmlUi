#include "../include/output_policy.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Presentation {

namespace {

double safe_positive(double value, double fallback) {
    return (std::isfinite(value) && value > 0.) ? value : fallback;
}

} // namespace

OutputPlacement output_placement_compute(double surfaceW, double surfaceH,
                                       double logicalW, double logicalH,
                                       OutputPolicy policy) {
    OutputPlacement layout{};
    surfaceW = safe_positive(surfaceW, 1.);
    surfaceH = safe_positive(surfaceH, 1.);
    logicalW = safe_positive(logicalW, 1.);
    logicalH = safe_positive(logicalH, 1.);

    layout.srcX = 0.;
    layout.srcY = 0.;
    layout.srcW = logicalW;
    layout.srcH = logicalH;

    if (policy == OutputPolicy::Stretch) {
        layout.destX = 0.;
        layout.destY = 0.;
        layout.destW = surfaceW;
        layout.destH = surfaceH;
        layout.scaleX = surfaceW / logicalW;
        layout.scaleY = surfaceH / logicalH;
        return layout;
    }

    const double sx = surfaceW / logicalW;
    const double sy = surfaceH / logicalH;
    double scale = (policy == OutputPolicy::Fill)
        ? std::max(sx, sy)
        : std::min(sx, sy);
    scale = std::max(0.01, scale);
    if (policy == OutputPolicy::Fit && scale >= 1.)
        scale = std::max(1., std::floor(scale));

    if (policy == OutputPolicy::Fill) {
        layout.destX = 0.;
        layout.destY = 0.;
        layout.destW = surfaceW;
        layout.destH = surfaceH;
        layout.srcW = surfaceW / scale;
        layout.srcH = surfaceH / scale;
        layout.srcX = (logicalW - layout.srcW) * 0.5;
        layout.srcY = (logicalH - layout.srcH) * 0.5;
        layout.scaleX = scale;
        layout.scaleY = scale;
        return layout;
    }

    layout.destW = logicalW * scale;
    layout.destH = logicalH * scale;
    layout.destX = (surfaceW - layout.destW) * 0.5;
    layout.destY = (surfaceH - layout.destH) * 0.5;
    layout.scaleX = scale;
    layout.scaleY = scale;
    return layout;
}

} // namespace ArtCade::Presentation
