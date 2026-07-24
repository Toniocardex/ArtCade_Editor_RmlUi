// frame-coordinator-test.cpp — FrameCoordinator commits presentation from scene authority.



#include "../src/app/render/frame_coordinator.h"

#include "../src/modules/scene-system/include/scene-manager.h"

#include "../src/modules/renderer/include/renderer.h"

#include "../src/modules/presentation/include/editor_viewport_service.h"

#include "../src/modules/presentation/include/presentation_input_builder.h"



#include <cmath>

#include <cstdio>

#include <cstdlib>



static bool near_eq(float a, float b, float eps = 0.001f) {

    return std::fabs(a - b) <= eps;

}



static void expect(bool ok, const char* msg) {

    if (!ok) {

        std::fprintf(stderr, "FAIL: %s\n", msg);

        std::exit(1);

    }

    std::printf("  [ok] %s\n", msg);

}



int main() {

    ArtCade::Modules::SceneManager scenes;

    scenes.init();



    ArtCade::SceneDef scene{};

    scene.id = "level";

    scene.worldSize = { 2048.f, 320.f };

    scene.viewportSize = { 800.f, 600.f };

    scenes.registerScenes({ { "level", scene } }, {});

    scenes.loadScene("level");



  const ArtCade::SceneDef* active = scenes.activeScene();



    ArtCade::Modules::Renderer renderer;

    renderer.setWindowSize(1280, 720, "frame-coordinator-test");



    ArtCade::Presentation::EditorViewportService viewport;

    viewport.set_presentation_mode(

        ArtCade::Presentation::PresentationMode::SceneEdit);



    const ArtCade::SceneFrameSnapshot snap = ArtCade::frame_coordinator_build_frame({

        1u,

        7u,

        active,

        &renderer,

        &viewport,

        {},

        0.f,

    });



    expect(snap.sceneRevision == 7u, "scene revision in snapshot");

    expect(snap.presentationRevision == snap.presentation.revision,

           "presentation revision consistent");

    expect(near_eq(snap.worldSize.x, 2048.f),

           "snapshot world from SceneDef not renderer impl");

    expect(near_eq(static_cast<float>(

        ArtCade::Presentation::presentation_build_inputs(

            active, renderer.gatherSimulationPresentationInputs()).worldWidth),

        2048.f),

           "coordinator committed scene-authoritative presentation");



    std::puts("frame_coordinator_test: all passed");

    return 0;

}


