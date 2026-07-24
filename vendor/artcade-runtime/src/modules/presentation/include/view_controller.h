#pragma once

#include "presentation_types.h"

namespace ArtCade::Presentation {

/**
 * Owns editor-view navigation intents (pan, zoom-at-cursor, resize).
 * Does not depend on renderer or Raylib.
 */
class ViewController {
public:
    void set_editor_view(EditorViewState view);
    const EditorViewState& editor_view() const { return editorView_; }

    void set_surface_metrics(SurfaceMetrics metrics);
    const SurfaceMetrics& surface_metrics() const { return surface_; }

    void begin_pan(SurfacePoint surface);
    void update_pan(SurfacePoint surface);
    void end_pan();

    /**
     * Cursor-anchored zoom for editor navigation.
     * The world point under @p surface stays fixed after zoom.
     * @param zoomFactor multiplicative factor (must be > 0)
     */
    void zoom_at(SurfacePoint surface, double zoomFactor);

    /** Updates surface metrics only; editor camera world position is preserved. */
    void resize_surface(double cssW, double cssH, double devicePixelRatio);

    /** Frames world bounds to fill the editor surface (no inner padding). */
    void frame_world_bounds(double minX, double minY, double maxX, double maxY);

    /**
     * Frames a selection centred on world position, widened by entity scale.
     * @param paddingPx inner padding subtracted from surface size before fit
     */
    void frame_selection_at(double posX,
                            double posY,
                            double scaleX,
                            double scaleY,
                            double paddingPx);

    /** Phase 2+: reset editor view. No-op stub in Phase 1. */
    void reset_view();

private:
    EditorViewState editorView_{};
    SurfaceMetrics surface_{};

    ViewCamera2D editor_camera() const;
    OutputPlacement editor_placement() const;

    void frame_world_bounds_with_padding(double minX,
                                         double minY,
                                         double maxX,
                                         double maxY,
                                         double paddingPx);
};

} // namespace ArtCade::Presentation
