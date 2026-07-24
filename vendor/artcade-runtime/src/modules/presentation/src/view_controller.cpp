#include "../include/view_controller.h"

#include "../include/coordinate_mapper.h"
#include "../include/editor_zoom_policy.h"
#include "../include/frame_selection.h"
#include "../include/surface_metrics.h"

#include <algorithm>

namespace ArtCade::Presentation {

namespace {

double safe_zoom(double zoom) {
    return (zoom > 0.) ? zoom : 1.;
}

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

} // namespace

void ViewController::set_editor_view(EditorViewState view) {
    editorView_ = view;
}

void ViewController::set_surface_metrics(SurfaceMetrics metrics) {
    surface_ = metrics;
}

ViewCamera2D ViewController::editor_camera() const {
    return {
        editorView_.positionX,
        editorView_.positionY,
        0.,
        0.,
        safe_zoom(editorView_.zoom),
    };
}

OutputPlacement ViewController::editor_placement() const {
    return identity_surface_placement(surface_.framebufferWidth,
                                      surface_.framebufferHeight);
}

void ViewController::begin_pan(SurfacePoint surface) {
    editorView_.panActive = true;
    editorView_.panAnchorSurfaceX = surface.x;
    editorView_.panAnchorSurfaceY = surface.y;
    editorView_.panAnchorPositionX = editorView_.positionX;
    editorView_.panAnchorPositionY = editorView_.positionY;
}

void ViewController::update_pan(SurfacePoint surface) {
    if (!editorView_.panActive)
        return;
    const ViewCamera2D camera = editor_camera();
    const OutputPlacement placement = editor_placement();
    const WorldPoint anchorWorld = world_from_surface(
        SurfacePoint{ editorView_.panAnchorSurfaceX, editorView_.panAnchorSurfaceY },
        placement,
        camera);
    const WorldPoint pointerWorld = world_from_surface(surface, placement, camera);
    editorView_.positionX = editorView_.panAnchorPositionX
        + (anchorWorld.x - pointerWorld.x);
    editorView_.positionY = editorView_.panAnchorPositionY
        + (anchorWorld.y - pointerWorld.y);
}

void ViewController::end_pan() {
    editorView_.panActive = false;
}

void ViewController::zoom_at(SurfacePoint surface, double zoomFactor) {
    if (!(zoomFactor > 0.))
        return;
    const OutputPlacement placement = editor_placement();
    const ViewCamera2D beforeCamera = editor_camera();
    const WorldPoint worldBefore = world_from_surface(surface, placement, beforeCamera);

    editorView_.zoom = editor_zoom_clamp(editorView_.zoom * zoomFactor);

    const ViewCamera2D afterCamera = editor_camera();
    const WorldPoint worldAfter = world_from_surface(surface, placement, afterCamera);
    editorView_.positionX += worldBefore.x - worldAfter.x;
    editorView_.positionY += worldBefore.y - worldAfter.y;
}

void ViewController::resize_surface(double cssW, double cssH, double devicePixelRatio) {
    surface_ = surface_metrics_from_css(cssW, cssH, devicePixelRatio);
}

void ViewController::frame_world_bounds(double minX, double minY,
                                        double maxX, double maxY) {
    frame_world_bounds_with_padding(minX, minY, maxX, maxY, 0.);
}

void ViewController::frame_selection_at(double posX,
                                        double posY,
                                        double scaleX,
                                        double scaleY,
                                        double paddingPx) {
    const FrameWorldBounds bounds =
        frame_selection_world_bounds(posX, posY, scaleX, scaleY);
    frame_world_bounds_with_padding(
        bounds.minX, bounds.minY, bounds.maxX, bounds.maxY, paddingPx);
}

void ViewController::frame_world_bounds_with_padding(double minX,
                                                     double minY,
                                                     double maxX,
                                                     double maxY,
                                                     double paddingPx) {
    const double boundsW = std::max(0., maxX - minX);
    const double boundsH = std::max(0., maxY - minY);
    if (boundsW <= 0. || boundsH <= 0.)
        return;

    const double fbW = surface_.framebufferWidth > 0.
        ? surface_.framebufferWidth
        : 1.;
    const double fbH = surface_.framebufferHeight > 0.
        ? surface_.framebufferHeight
        : 1.;
    const double availW = std::max(1., fbW - paddingPx);
    const double availH = std::max(1., fbH - paddingPx);
    const double scaleX = availW / boundsW;
    const double scaleY = availH / boundsH;
    editorView_.zoom = editor_zoom_clamp(std::min(scaleX, scaleY));
    editorView_.positionX = minX;
    editorView_.positionY = minY;
}

void ViewController::reset_view() {
    editorView_.positionX = 0.;
    editorView_.positionY = 0.;
    editorView_.zoom = 1.;
}

} // namespace ArtCade::Presentation
