#pragma once

#include "presentation_mode.h"
#include "presentation_state.h"
#include "presentation_types.h"

namespace ArtCade::Presentation {

class PresentationSystem;

/** Renderer-owned inputs copied into PresentationState before solving. */
struct PresentationStateInputs {
    PresentationMode mode = PresentationMode::SceneEdit;
    OutputPolicy outputPolicy = OutputPolicy::Fit;
    SurfaceMetrics surface{};
    double logicalWidth = 1.;
    double logicalHeight = 1.;
    double worldWidth = 1.;
    double worldHeight = 1.;
    bool gameViewCompositorEnabled = false;
    EditorCamera editorCamera{};
    GameCameraState gameCamera{};
    CameraModifiers gameModifiers{};
};

/**
 * Copies @p inputs into @p state without altering placement or picking fields.
 * @param state presentation state owned by PresentationSystem
 * @param inputs snapshot of renderer/editor inputs
 */
void presentation_apply_state_inputs(PresentationState& state,
                                     const PresentationStateInputs& inputs);

/** Applies @p inputs to @p system without exposing mutable_state to callers. */
void presentation_sync_system_inputs(PresentationSystem& system,
                                     const PresentationStateInputs& inputs);

} // namespace ArtCade::Presentation
