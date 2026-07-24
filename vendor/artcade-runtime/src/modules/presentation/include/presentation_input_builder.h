#pragma once

#include "../../../core/types.h"
#include "presentation_state_sync.h"

namespace ArtCade::Presentation {

/**
 * Simulation-owned presentation inputs (camera, output policy, compositor).
 * Scene geometry (world / logical viewport) is supplied separately via SceneDef.
 */
struct PresentationSimulationInputs {
    OutputPolicy outputPolicy = OutputPolicy::Fit;
    bool gameViewCompositorEnabled = false;
    GameCameraState gameCamera{};
    CameraModifiers gameModifiers{};
};

/**
 * Builds presentation inputs from authoritative scene geometry + simulation state.
 * @param scene active SceneDef (nullptr uses ProjectDefaults)
 * @param sim renderer-owned camera / policy / compositor flags
 */
PresentationStateInputs presentation_build_inputs(
    const SceneDef* scene,
    const PresentationSimulationInputs& sim);

} // namespace ArtCade::Presentation
