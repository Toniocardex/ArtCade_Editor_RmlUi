#include "../include/surface_metrics.h"

#include <cmath>

namespace ArtCade::Presentation {

SurfaceMetrics surface_metrics_from_css(double cssW, double cssH,
                                      double devicePixelRatio) {
    SurfaceMetrics metrics{};
    metrics.cssWidth = std::max(0., cssW);
    metrics.cssHeight = std::max(0., cssH);
    metrics.devicePixelRatio = (devicePixelRatio > 0. && std::isfinite(devicePixelRatio))
        ? devicePixelRatio
        : 1.;
    metrics.framebufferWidth = std::round(metrics.cssWidth * metrics.devicePixelRatio);
    metrics.framebufferHeight = std::round(metrics.cssHeight * metrics.devicePixelRatio);
    if (metrics.framebufferWidth < 1.)
        metrics.framebufferWidth = 1.;
    if (metrics.framebufferHeight < 1.)
        metrics.framebufferHeight = 1.;
    return metrics;
}

SurfacePoint surface_point_from_css(CssPoint css, const SurfaceMetrics& metrics) {
    const double cssW = metrics.cssWidth > 0. ? metrics.cssWidth : 1.;
    const double cssH = metrics.cssHeight > 0. ? metrics.cssHeight : 1.;
    const double scaleX = metrics.framebufferWidth / cssW;
    const double scaleY = metrics.framebufferHeight / cssH;
    return {
        css.x * scaleX,
        css.y * scaleY,
    };
}

} // namespace ArtCade::Presentation
