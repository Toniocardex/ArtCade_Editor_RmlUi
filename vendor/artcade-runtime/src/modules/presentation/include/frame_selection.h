#pragma once

namespace ArtCade::Presentation {

/** Axis-aligned world bounds used by ViewController framing. */
struct FrameWorldBounds {
    double minX = 0.;
    double minY = 0.;
    double maxX = 0.;
    double maxY = 0.;
};

/** Default world span framed around a selection at unit scale (one scene height). */
constexpr double FRAME_SELECTION_SPAN = 320.;

/**
 * Builds world bounds centred on @p posX/@p posY, widened by entity scale.
 * @param scaleX entity scale X (0 uses 1)
 * @param scaleY entity scale Y (0 uses 1)
 */
FrameWorldBounds frame_selection_world_bounds(double posX,
                                              double posY,
                                              double scaleX,
                                              double scaleY);

} // namespace ArtCade::Presentation
