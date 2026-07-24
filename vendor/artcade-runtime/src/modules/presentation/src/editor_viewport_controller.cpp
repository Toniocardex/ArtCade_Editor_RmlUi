#include "../include/editor_viewport_controller.h"

#include "../include/surface_metrics.h"

namespace ArtCade::Presentation {

void EditorViewportController::set_surface_metrics(const SurfaceMetrics& metrics) {
    viewController_.set_surface_metrics(metrics);
}

void EditorViewportController::resize_surface(double cssWidth,
                                              double cssHeight,
                                              double devicePixelRatio) {
    viewController_.resize_surface(cssWidth, cssHeight, devicePixelRatio);
}

void EditorViewportController::begin_pan(SurfacePoint surface) {
    viewController_.begin_pan(surface);
}

void EditorViewportController::update_pan(SurfacePoint surface) {
    viewController_.update_pan(surface);
}

void EditorViewportController::end_pan() {
    viewController_.end_pan();
}

void EditorViewportController::zoom_at(SurfacePoint surface, double factor) {
    viewController_.zoom_at(surface, factor);
}

void EditorViewportController::frame_world_bounds(double minX,
                                                  double minY,
                                                  double maxX,
                                                  double maxY) {
    viewController_.frame_world_bounds(minX, minY, maxX, maxY);
}

void EditorViewportController::frame_selection_at(double posX,
                                                double posY,
                                                double scaleX,
                                                double scaleY,
                                                double paddingPx) {
    viewController_.frame_selection_at(posX, posY, scaleX, scaleY, paddingPx);
}

void EditorViewportController::reset_view() {
    viewController_.reset_view();
}

void EditorViewportController::set_editor_view(const EditorViewState& view) {
    viewController_.set_editor_view(view);
}

EditorViewState EditorViewportController::editor_view() const {
    return viewController_.editor_view();
}

void EditorViewportController::begin_frame() {
    presentation_.begin_frame();
}

void EditorViewportController::refresh_pending_snapshot() {
    presentation_.refresh_pending_snapshot();
}

} // namespace ArtCade::Presentation
