#pragma once

#include "editor_viewport_controller.h"
#include "editor_viewport_navigation.h"
#include "presentation_mode.h"
#include "presentation_snapshot.h"
#include "presentation_state_sync.h"
#include "presentation_input_builder.h"
#include "presentation_types.h"
#include "surface_metrics.h"

namespace ArtCade::Presentation {

/**
 * Owns editor viewport navigation (ViewController + PresentationSystem) and the
 * mutable host context (editor camera, mode, DPR). Application wires this to
 * EditorAPI; Renderer is a read-only consumer of committed snapshots.
 */
class EditorViewportService {
public:
    EditorViewportController& controller() noexcept { return controller_; }
    const EditorViewportController& controller() const noexcept {
        return controller_;
    }

    EditorViewportHost& host() noexcept { return host_; }
    const EditorViewportHost& host() const noexcept { return host_; }

    void set_presentation_mode(PresentationMode mode);
    PresentationMode presentation_mode() const { return host_.presentationMode; }

    /** Applies editor camera target/zoom verbatim (SceneEdit). */
    void set_editor_camera(double positionX, double positionY, double zoom);

    /**
     * Copies @p host editor view into the controller and updates surface metrics.
     * @param surface framebuffer/CSS metrics for the current frame
     */
    void prepare(const SurfaceMetrics& surface);

    /** Writes controller editor view back into @p host and enters scene edit. */
    void commit();

    /**
     * Merges simulation inputs with host editor state, then syncs.
     * Scene geometry must come from SceneDef via sync_from_scene().
     */
    void sync_from_scene(const SceneDef* scene,
                         const PresentationSimulationInputs& sim,
                         uint32_t framebufferWidth,
                         uint32_t framebufferHeight);

    /**
     * Copies renderer/simulation inputs into presentation state (pending only).
     * @param inputs fully composed presentation inputs
     */
    void sync_inputs(const PresentationStateInputs& inputs);

    /** Recalculates the draft snapshot; does not commit. */
    void refresh_pending_snapshot();

    /** Commits presentation for the current frame (sole revision bump path). */
    void begin_frame();

    /**
     * Builds surface metrics from host CSS/DPR and the live framebuffer size.
     * @param framebufferWidth device pixels (must be >= 1)
     * @param framebufferHeight device pixels (must be >= 1)
     */
    SurfaceMetrics surface_metrics(uint32_t framebufferWidth,
                                   uint32_t framebufferHeight) const;

    /**
     * Editor canvas resize: updates host metrics, preserves camera, syncs controller.
     * Does not resize the OS framebuffer — caller must set window size first.
     */
    void resize_editor_surface(double cssWidth,
                               double cssHeight,
                               double devicePixelRatio,
                               uint32_t framebufferWidth,
                               uint32_t framebufferHeight);

    /**
     * Play-mode surface resize: stores CSS host size × DPR for the next commit.
     * Does not resize the OS framebuffer — caller must set window size first.
     */
    void sync_play_surface(double cssWidth,
                           double cssHeight,
                           double devicePixelRatio,
                           uint32_t framebufferWidth,
                           uint32_t framebufferHeight);

    /** Copies host editor view into the controller for navigation (pan/zoom/frame). */
    void navigation_prepare(uint32_t framebufferWidth, uint32_t framebufferHeight);

    /** Writes controller editor view back into host after navigation. */
    void navigation_commit();

    /** Resets editor view; requires navigation prepare/commit around framebuffer size. */
    void reset_editor_view(uint32_t framebufferWidth, uint32_t framebufferHeight);

    const PresentationSnapshot& committed_snapshot() const {
        return controller_.committed_snapshot();
    }

    uint64_t presentation_revision() const {
        return controller_.committed_snapshot().revision;
    }

    /**
     * Maps surface pixels to world using a specific revision when still in history.
     * Falls back to committed snapshot when @p revision is not found.
     */
    WorldPoint surface_to_world_at_revision(float surfaceX,
                                            float surfaceY,
                                            uint64_t revision) const;

    bool scene_edit_active() const { return host_.scene_edit_active(); }

private:
    EditorViewportController controller_;
    EditorViewportHost host_;
};

} // namespace ArtCade::Presentation
