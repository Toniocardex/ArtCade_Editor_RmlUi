#include "../include/editor_zoom_policy.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Presentation {

double editor_zoom_clamp(double zoom) {
    if (!std::isfinite(zoom))
        return 1.;
    const double clamped = std::min(kEditorZoomMax, std::max(kEditorZoomMin, zoom));
    const double snap = std::pow(10., static_cast<double>(kEditorZoomSnapDecimals));
    return std::round(clamped * snap) / snap;
}

} // namespace ArtCade::Presentation
