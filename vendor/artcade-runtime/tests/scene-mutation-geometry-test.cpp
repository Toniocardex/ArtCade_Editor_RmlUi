// scene-mutation-geometry-test.cpp — geometry patch propagates to presentation inputs.

#include "../src/modules/scene-system/include/scene-manager.h"
#include "../src/modules/scene-system/include/scene-mutation-service.h"
#include "../src/modules/scene-system/include/scene-invalidation.h"
#include "../src/modules/renderer/include/renderer.h"
#include "../src/modules/presentation/include/editor_viewport_service.h"
#include "../src/modules/presentation/include/presentation_input_builder.h"
#include "presentation_test_helpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Modules::Renderer;
using ArtCade::Modules::SceneManager;
using ArtCade::Modules::SceneMutationService;
using ArtCade::Modules::ScenePatch;
using ArtCade::Presentation::EditorViewportService;
using ArtCade::Presentation::presentation_build_inputs;

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
    SceneManager scenes;
    scenes.init();

    ArtCade::SceneDef scene{};
    scene.id = "s";
    scene.name = "Test";
    scene.worldSize = { 512.f, 320.f };
    scene.viewportSize = { 512.f, 320.f };
    scenes.registerScenes({ { "s", scene } }, {});
    scenes.loadScene("s");

    Renderer renderer;
    renderer.setWindowSize(1280, 720, "scene-mutation-geometry-test");
    renderer.setCameraZoom(1.5f);
    renderer.setCameraPosition({ 100.f, 50.f });

    const float zoomBefore = renderer.getCameraZoom();
    const ArtCade::Vec2 cameraBefore = renderer.getCameraPosition();

    SceneMutationService mutation(scenes);
    ScenePatch patch{};
    patch.worldSize = { 2048.f, 320.f };
    patch.hasWorldSize = true;
    patch.viewportSize = { 512.f, 320.f };
    patch.hasViewportSize = true;

    const auto result = mutation.apply("s", patch);
    expect(result.changed, "world resize mutates scene");
    expect(result.sceneRevision >= 1u, "scene revision increments");
    expect(
        ArtCade::Modules::scene_invalidation_has(
            result.invalidations, ArtCade::Modules::SceneInvalidation::Collision),
        "collision invalidation emitted");

    const auto* active = scenes.activeScene();
    expect(active != nullptr, "active scene exists");
    expect(near_eq(active->worldSize.x, 2048.f), "SceneDef world width is 2048");

    const auto sceneAuthority = presentation_build_inputs(
        active, renderer.gatherSimulationPresentationInputs());
    expect(near_eq(static_cast<float>(sceneAuthority.worldWidth), 2048.f),
           "presentation inputs from SceneDef report 2048 before frame commit");
    expect(near_eq(static_cast<float>(sceneAuthority.worldHeight), 320.f),
           "presentation world height unchanged");

    EditorViewportService viewport;
    viewport.set_presentation_mode(
        ArtCade::Presentation::PresentationMode::SceneEdit);

    commit_presentation_frame(renderer, viewport, active);
    const auto& snapshot = viewport.committed_snapshot();
    expect(snapshot.revision >= 1u, "presentation revision committed");
    expect(near_eq(static_cast<float>(
        presentation_build_inputs(
            active, renderer.gatherSimulationPresentationInputs()).worldWidth),
        2048.f),
           "presentation inputs still report 2048 after commit");

    expect(near_eq(renderer.getCameraZoom(), zoomBefore), "camera zoom unchanged");
    expect(near_eq(renderer.getCameraPosition().x, cameraBefore.x)
           && near_eq(renderer.getCameraPosition().y, cameraBefore.y),
           "camera position unchanged");

    std::puts("scene_mutation_geometry_test: all passed");
    return 0;
}
