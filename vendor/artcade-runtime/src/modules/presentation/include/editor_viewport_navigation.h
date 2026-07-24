#pragma once

#include "editor_viewport_controller.h"
#include "presentation_mode.h"
#include "presentation_types.h"

namespace ArtCade::Presentation {

/**
 * Mutable editor-navigation context owned by EditorViewportService (Application).
 * Editor WASM targets this through editor_viewport_navigation helpers.
 */
struct EditorViewportHost {
    EditorCamera editorCamera{};
    PresentationMode presentationMode = PresentationMode::SceneEdit;
    float editorSurfaceDpr = 1.f;
    double surfaceCssWidth = 0.;
    double surfaceCssHeight = 0.;

    bool scene_edit_active() const {
        return presentationMode == PresentationMode::SceneEdit;
    }

    void enter_scene_edit() {
        presentationMode = PresentationMode::SceneEdit;
    }
};

SurfacePoint editor_viewport_css_to_surface(float cssX,
                                            float cssY,
                                            float devicePixelRatio);

void editor_viewport_prepare(EditorViewportController& controller,
                             EditorViewportHost& host,
                             const SurfaceMetrics& surface);

void editor_viewport_commit(EditorViewportController& controller,
                            EditorViewportHost& host);

} // namespace ArtCade::Presentation
