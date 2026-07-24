#include "../include/presentation_solver.h"

#include "../include/camera_compose.h"
#include "../include/coordinate_mapper.h"
#include "../include/output_policy.h"
#include "../include/presentation_mode.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Presentation {

namespace {

OutputPlacement identity_surface_placement(double surfaceW, double surfaceH) {
    OutputPlacement placement{};
    placement.destW = surfaceW;
    placement.destH = surfaceH;
    placement.srcW = surfaceW;
    placement.srcH = surfaceH;
    placement.scaleX = 1.;
    placement.scaleY = 1.;
    return placement;
}

bool letterbox_active(const OutputPlacement& placement,
                      double surfaceW,
                      double surfaceH) {
    return placement.destX > 0.
        || placement.destY > 0.
        || placement.destW < surfaceW
        || placement.destH < surfaceH;
}

ViewCamera2D picking_camera_from_game(const GameCameraState& base,
                                      const CameraModifiers& modifiers,
                                      double logicalW,
                                      double logicalH,
                                      double worldW,
                                      double worldH) {
    const EffectiveGameCamera effective =
        compose_effective_game_camera(base, modifiers);
    const double zoom = effective.zoom > 0. ? effective.zoom : 1.;
    const double insetX = std::max(0., (logicalW - worldW * zoom) * 0.5);
    const double insetY = std::max(0., (logicalH - worldH * zoom) * 0.5);
    return {
        effective.positionX,
        effective.positionY,
        insetX,
        insetY,
        zoom,
    };
}

void fill_visible_world_bounds(PresentationSnapshot& snapshot) {
    const double fbW = snapshot.surface.framebufferWidth;
    const double fbH = snapshot.surface.framebufferHeight;
    if (!(fbW > 0.) || !(fbH > 0.)) return;

    const OutputPlacement placement = snapshot.useIdentityPlacement
        ? identity_surface_placement(fbW, fbH)
        : snapshot.placement;

    const WorldPoint topLeft = world_from_surface(
        SurfacePoint{ 0., 0. }, placement, snapshot.pickingCamera);
    const WorldPoint bottomRight = world_from_surface(
        SurfacePoint{ fbW, fbH }, placement, snapshot.pickingCamera);

    snapshot.visibleWorldMinX = std::min(topLeft.x, bottomRight.x);
    snapshot.visibleWorldMinY = std::min(topLeft.y, bottomRight.y);
    snapshot.visibleWorldMaxX = std::max(topLeft.x, bottomRight.x);
    snapshot.visibleWorldMaxY = std::max(topLeft.y, bottomRight.y);
}

} // namespace

PresentationSnapshot solve_presentation_snapshot(const PresentationState& state,
                                                 uint64_t revision) {
    PresentationSnapshot snapshot{};
    snapshot.revision = revision;
    snapshot.effectiveMode = state.mode;
    snapshot.surface = state.surface;
    snapshot.logicalWidth = state.logicalWidth;
    snapshot.logicalHeight = state.logicalHeight;
    snapshot.useIdentityPlacement = state.mode == PresentationMode::SceneEdit
        && !state.gameViewCompositorEnabled;

    const double fbW = state.surface.framebufferWidth;
    const double fbH = state.surface.framebufferHeight;
    const double logicalW = state.logicalWidth > 0. ? state.logicalWidth : 1.;
    const double logicalH = state.logicalHeight > 0. ? state.logicalHeight : 1.;
    const double worldW = state.worldWidth > 0. ? state.worldWidth : logicalW;
    const double worldH = state.worldHeight > 0. ? state.worldHeight : logicalH;

    if (state.mode == PresentationMode::SceneEdit
        && !state.gameViewCompositorEnabled) {
        snapshot.placement = identity_surface_placement(fbW, fbH);
        snapshot.pickingCamera = view_camera_from_editor(state.editorCamera);
        snapshot.editorViewOriginX = state.editorCamera.positionX;
        snapshot.editorViewOriginY = state.editorCamera.positionY;
        const double editorZoom = state.editorCamera.zoom > 0.
            ? state.editorCamera.zoom
            : 1.;
        snapshot.surfacePixelsPerWorldUnit = editorZoom;
    } else {
        const OutputPolicy policy = state.gameViewCompositorEnabled
            ? state.outputPolicy
            : OutputPolicy::Fit;
        snapshot.placement = output_placement_compute(
            fbW, fbH, logicalW, logicalH, policy);
        snapshot.pickingCamera = picking_camera_from_game(
            state.gameCamera,
            state.gameModifiers,
            logicalW,
            logicalH,
            worldW,
            worldH);
        snapshot.editorViewOriginX = snapshot.pickingCamera.targetX;
        snapshot.editorViewOriginY = snapshot.pickingCamera.targetY;
        const double scale = snapshot.placement.scaleX > 0.
            ? snapshot.placement.scaleX
            : 1.;
        const double zoom = snapshot.pickingCamera.zoom > 0.
            ? snapshot.pickingCamera.zoom
            : 1.;
        snapshot.surfacePixelsPerWorldUnit = scale * zoom;
    }

    snapshot.presentationScale = snapshot.placement.scaleX;
    snapshot.letterboxActive = letterbox_active(snapshot.placement, fbW, fbH);
    fill_visible_world_bounds(snapshot);
    return snapshot;
}

} // namespace ArtCade::Presentation
