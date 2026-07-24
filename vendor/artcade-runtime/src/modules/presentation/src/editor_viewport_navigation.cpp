#include "../include/editor_viewport_navigation.h"

namespace ArtCade::Presentation {

SurfacePoint editor_viewport_css_to_surface(float cssX,
                                            float cssY,
                                            float devicePixelRatio) {
    const double scale = devicePixelRatio > 0.f
        ? static_cast<double>(devicePixelRatio)
        : 1.;
    return {
        static_cast<double>(cssX) * scale,
        static_cast<double>(cssY) * scale,
    };
}

void editor_viewport_prepare(EditorViewportController& controller,
                             EditorViewportHost& host,
                             const SurfaceMetrics& surface) {
    EditorViewState view{};
    view.positionX = host.editorCamera.positionX;
    view.positionY = host.editorCamera.positionY;
    view.zoom = host.editorCamera.zoom > 0. ? host.editorCamera.zoom : 1.;
    controller.set_editor_view(view);
    controller.set_surface_metrics(surface);
}

void editor_viewport_commit(EditorViewportController& controller,
                            EditorViewportHost& host) {
    const EditorViewState& view = controller.editor_view();
    host.editorCamera.positionX = view.positionX;
    host.editorCamera.positionY = view.positionY;
    host.editorCamera.zoom = view.zoom > 0. ? view.zoom : 1.;
    host.enter_scene_edit();
}

} // namespace ArtCade::Presentation
