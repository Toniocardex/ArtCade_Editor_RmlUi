#include "../include/frame_selection.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Presentation {

namespace {

constexpr double kMaxScaleFactor = 8.;

double scale_factor(double scale) {
    const double magnitude = std::abs(scale);
    const double base = magnitude > 0. ? magnitude : 1.;
    return std::min(kMaxScaleFactor, std::max(1., base));
}

} // namespace

FrameWorldBounds frame_selection_world_bounds(double posX,
                                              double posY,
                                              double scaleX,
                                              double scaleY) {
    const double spanX = FRAME_SELECTION_SPAN * scale_factor(scaleX);
    const double spanY = FRAME_SELECTION_SPAN * scale_factor(scaleY);
    const double halfX = spanX * 0.5;
    const double halfY = spanY * 0.5;
    return {
        posX - halfX,
        posY - halfY,
        posX + halfX,
        posY + halfY,
    };
}

} // namespace ArtCade::Presentation
