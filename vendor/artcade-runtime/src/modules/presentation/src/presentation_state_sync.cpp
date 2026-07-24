#include "../include/presentation_state_sync.h"

#include "../include/presentation_system.h"

namespace ArtCade::Presentation {

void presentation_apply_state_inputs(PresentationState& state,
                                     const PresentationStateInputs& inputs) {
    state.mode = inputs.mode;
    state.outputPolicy = inputs.outputPolicy;
    state.surface = inputs.surface;
    state.logicalWidth = inputs.logicalWidth;
    state.logicalHeight = inputs.logicalHeight;
    state.worldWidth = inputs.worldWidth;
    state.worldHeight = inputs.worldHeight;
    state.gameViewCompositorEnabled = inputs.gameViewCompositorEnabled;
    state.editorCamera = inputs.editorCamera;
    state.gameCamera = inputs.gameCamera;
    state.gameModifiers = inputs.gameModifiers;
}

void presentation_sync_system_inputs(PresentationSystem& system,
                                     const PresentationStateInputs& inputs) {
    presentation_apply_state_inputs(system.mutable_state(), inputs);
}

} // namespace ArtCade::Presentation
