#include "../include/presentation_input_builder.h"

#include "../../../core/project-defaults.h"

#include <algorithm>

namespace ArtCade::Presentation {

PresentationStateInputs presentation_build_inputs(
    const SceneDef* scene,
    const PresentationSimulationInputs& sim) {
    PresentationStateInputs inputs{};
    inputs.outputPolicy = sim.outputPolicy;
    inputs.gameViewCompositorEnabled = sim.gameViewCompositorEnabled;
    inputs.gameCamera = sim.gameCamera;
    inputs.gameModifiers = sim.gameModifiers;

    if (scene) {
        inputs.logicalWidth = static_cast<double>(
            std::max(1.f, scene->viewportSize.x));
        inputs.logicalHeight = static_cast<double>(
            std::max(1.f, scene->viewportSize.y));
        inputs.worldWidth = static_cast<double>(
            std::max(1.f, scene->worldSize.x));
        inputs.worldHeight = static_cast<double>(
            std::max(1.f, scene->worldSize.y));
    } else {
        inputs.logicalWidth = static_cast<double>(
            ProjectDefaults::kSceneViewportWidth);
        inputs.logicalHeight = static_cast<double>(
            ProjectDefaults::kSceneViewportHeight);
        inputs.worldWidth = static_cast<double>(
            ProjectDefaults::kSceneWorldWidth);
        inputs.worldHeight = static_cast<double>(
            ProjectDefaults::kSceneWorldHeight);
    }
    return inputs;
}

} // namespace ArtCade::Presentation
