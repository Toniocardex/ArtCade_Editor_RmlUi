#pragma once

#include "presentation_mode.h"
#include "presentation_types.h"

namespace ArtCade::Presentation {

/**
 * Immutable presentation result committed once per frame.
 * Does not contain render-pass instructions.
 */
struct PresentationSnapshot {
    uint64_t revision = 0;
    PresentationMode effectiveMode = PresentationMode::SceneEdit;
    SurfaceMetrics surface;
    double logicalWidth = 1.;
    double logicalHeight = 1.;
    OutputPlacement placement{};
    bool useIdentityPlacement = false;
    ViewCamera2D pickingCamera{};
    double presentationScale = 1.;
    bool letterboxActive = false;
    /** Editor camera top-left in world space (SceneEdit). */
    double editorViewOriginX = 0.;
    double editorViewOriginY = 0.;
    /** Surface pixels per world unit at the committed frame. */
    double surfacePixelsPerWorldUnit = 1.;
    double visibleWorldMinX = 0.;
    double visibleWorldMinY = 0.;
    double visibleWorldMaxX = 0.;
    double visibleWorldMaxY = 0.;

    /**
     * Maps framebuffer coordinates to world using this snapshot's placement
     * and picking camera (committed for the frame).
     */
    WorldPoint surface_to_world(SurfacePoint surface) const;

    /** Inverse of surface_to_world. */
    SurfacePoint world_to_surface(WorldPoint world) const;
};

} // namespace ArtCade::Presentation
