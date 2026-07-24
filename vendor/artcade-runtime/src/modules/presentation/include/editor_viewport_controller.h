#pragma once

#include "presentation_system.h"
#include "presentation_types.h"
#include "view_controller.h"

namespace ArtCade::Presentation {

/**
 * Owns editor view navigation (ViewController) and presentation commits.
 * Renderer consumes snapshots; editor WASM targets this controller.
 */
class EditorViewportController {
public:
    PresentationSystem& presentation() { return presentation_; }
    const PresentationSystem& presentation() const { return presentation_; }

    ViewController& view_controller() { return viewController_; }
    const ViewController& view_controller() const { return viewController_; }

    void set_surface_metrics(const SurfaceMetrics& metrics);

    void resize_surface(double cssWidth, double cssHeight, double devicePixelRatio);

    void begin_pan(SurfacePoint surface);
    void update_pan(SurfacePoint surface);
    void end_pan();

    void zoom_at(SurfacePoint surface, double factor);
    void frame_world_bounds(double minX, double minY, double maxX, double maxY);

    void frame_selection_at(double posX,
                            double posY,
                            double scaleX,
                            double scaleY,
                            double paddingPx);

    void reset_view();

    void set_editor_view(const EditorViewState& view);
    EditorViewState editor_view() const;

    void begin_frame();
    void refresh_pending_snapshot();

    const PresentationSnapshot& committed_snapshot() const {
        return presentation_.committed_snapshot();
    }

    const PresentationSnapshot& pending_snapshot() const {
        return presentation_.pending_snapshot();
    }

private:
    PresentationSystem presentation_;
    ViewController viewController_;
};

} // namespace ArtCade::Presentation
