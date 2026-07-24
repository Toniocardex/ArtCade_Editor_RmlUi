#include "../include/editor_viewport_service.h"

#include "../include/editor_viewport_navigation.h"
#include "../include/presentation_bindings.h"
#include "../include/presentation_input_builder.h"

#include <algorithm>

namespace ArtCade::Presentation {

void EditorViewportService::set_presentation_mode(PresentationMode mode) {
    host_.presentationMode = mode;
}

void EditorViewportService::set_editor_camera(double positionX,
                                              double positionY,
                                              double zoom) {
    const double safeZoom = zoom > 0. ? zoom : 1.;
    host_.editorCamera = { positionX, positionY, safeZoom, 0. };
    host_.enter_scene_edit();
    EditorViewState view{};
    view.positionX = positionX;
    view.positionY = positionY;
    view.zoom = safeZoom;
    controller_.set_editor_view(view);
}

SurfaceMetrics EditorViewportService::surface_metrics(uint32_t framebufferWidth,
                                                      uint32_t framebufferHeight) const {
    const double dpr = host_.editorSurfaceDpr > 0.f
        ? static_cast<double>(host_.editorSurfaceDpr)
        : 1.;
    const double fbW = static_cast<double>(std::max(1u, framebufferWidth));
    const double fbH = static_cast<double>(std::max(1u, framebufferHeight));
    const double cssW = host_.surfaceCssWidth > 0.
        ? host_.surfaceCssWidth
        : fbW / dpr;
    const double cssH = host_.surfaceCssHeight > 0.
        ? host_.surfaceCssHeight
        : fbH / dpr;
    auto metrics = surface_metrics_from_css(cssW, cssH, dpr);
    metrics.framebufferWidth = fbW;
    metrics.framebufferHeight = fbH;
    return metrics;
}

void EditorViewportService::prepare(const SurfaceMetrics& surface) {
    editor_viewport_prepare(controller_, host_, surface);
}

void EditorViewportService::commit() {
    editor_viewport_commit(controller_, host_);
}

void EditorViewportService::sync_from_scene(
    const SceneDef* scene,
    const PresentationSimulationInputs& sim,
    uint32_t framebufferWidth,
    uint32_t framebufferHeight) {
    PresentationStateInputs inputs = presentation_build_inputs(scene, sim);
    inputs.mode = host_.presentationMode;
    inputs.editorCamera = host_.editorCamera;
    inputs.surface = surface_metrics(framebufferWidth, framebufferHeight);
    sync_inputs(inputs);
}

void EditorViewportService::sync_inputs(const PresentationStateInputs& inputs) {
    presentation_sync_system_inputs(controller_.presentation(), inputs);
}

void EditorViewportService::refresh_pending_snapshot() {
    controller_.refresh_pending_snapshot();
}

void EditorViewportService::begin_frame() {
    controller_.begin_frame();
}

void EditorViewportService::resize_editor_surface(double cssWidth,
                                                  double cssHeight,
                                                  double devicePixelRatio,
                                                  uint32_t framebufferWidth,
                                                  uint32_t framebufferHeight) {
    const double safeDpr = devicePixelRatio > 0. ? devicePixelRatio : 1.;
    host_.editorSurfaceDpr = static_cast<float>(safeDpr);
    host_.surfaceCssWidth = cssWidth;
    host_.surfaceCssHeight = cssHeight;
    controller_.resize_surface(cssWidth, cssHeight, safeDpr);
    prepare(surface_metrics(framebufferWidth, framebufferHeight));
    commit();
}

void EditorViewportService::sync_play_surface(double cssWidth,
                                              double cssHeight,
                                              double devicePixelRatio,
                                              uint32_t framebufferWidth,
                                              uint32_t framebufferHeight) {
    const double safeDpr = devicePixelRatio > 0. ? devicePixelRatio : 1.;
    host_.editorSurfaceDpr = static_cast<float>(safeDpr);
    host_.surfaceCssWidth = cssWidth;
    host_.surfaceCssHeight = cssHeight;
}

void EditorViewportService::navigation_prepare(uint32_t framebufferWidth,
                                               uint32_t framebufferHeight) {
    prepare(surface_metrics(framebufferWidth, framebufferHeight));
}

void EditorViewportService::navigation_commit() {
    commit();
}

void EditorViewportService::reset_editor_view(uint32_t framebufferWidth,
                                              uint32_t framebufferHeight) {
    navigation_prepare(framebufferWidth, framebufferHeight);
    controller_.reset_view();
    navigation_commit();
}

WorldPoint EditorViewportService::surface_to_world_at_revision(
    float surfaceX, float surfaceY, uint64_t revision) const {
    return PresentationBindings::surface_to_world(
        controller_.presentation(),
        revision,
        SurfacePoint{ static_cast<double>(surfaceX),
                      static_cast<double>(surfaceY) });
}

} // namespace ArtCade::Presentation
