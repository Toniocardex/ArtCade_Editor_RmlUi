#include "frame_coordinator.h"

#include "scene_frame_snapshot.h"

#include "../../modules/editor-api/include/editor-api.h"
#include "../../modules/presentation/include/editor_viewport_service.h"
#include "../../modules/presentation/include/presentation_input_builder.h"
#include "../../modules/renderer/include/renderer.h"

#include <cassert>

namespace ArtCade {

SceneFrameSnapshot frame_coordinator_build_frame(const FrameCoordinatorInput& input) {
    assert(input.renderer && input.editorViewport);

    const auto sim = input.renderer->gatherSimulationPresentationInputs();
    input.editorViewport->sync_from_scene(
        input.activeScene,
        sim,
        input.renderer->windowWidth(),
        input.renderer->windowHeight());
    input.editorViewport->begin_frame();

    const auto& presentation = input.editorViewport->committed_snapshot();

    SceneFrameSnapshot snap = scene_frame_build({
        input.frameNumber,
        input.sceneRevision,
        input.activeScene,
        presentation,
        input.overlay,
        input.sceneFadeAlpha,
    });

#ifndef NDEBUG
    assert(snap.presentationRevision == snap.presentation.revision);
#endif

    EditorAPI::commit_scene_frame({
        snap.sceneRevision,
        snap.layerSettings,
    });

    return snap;
}

} // namespace ArtCade
