// presentation-camera-modes-test.cpp — Phase 3: dual camera, explicit modes, modifiers.

#include "../src/modules/renderer/include/renderer.h"
#include "../src/modules/presentation/include/editor_viewport_service.h"
#include "../src/modules/presentation/include/presentation_bindings.h"
#include "../src/modules/presentation/include/presentation_mode.h"
#include "../src/modules/presentation/include/presentation_types.h"
#include "presentation_test_helpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Modules::Renderer;
using ArtCade::Presentation::PresentationBindings;
using ArtCade::Presentation::EditorViewportService;
using ArtCade::Presentation::PresentationMode;
using ArtCade::Presentation::SurfacePoint;

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
    ArtCade::Presentation::EditorViewportService viewport;
    Renderer renderer;
    const ArtCade::Vec2 frameSize{ 1280.f, 720.f };
    ArtCade::SceneDef scene{};
    scene.worldSize = frameSize;
    scene.viewportSize = frameSize;

    renderer.setWindowSize(1280, 720, "camera-modes");
    renderer.setCameraZoom(2.f);
    renderer.setCameraPosition({ 100.f, 50.f });

    viewport.set_presentation_mode(PresentationMode::SceneEdit);
    viewport.set_editor_camera(400., 300., 1.5);
    const auto gamePos = renderer.getCameraPosition();
    expect(near_eq(gamePos.x, 100.f) && near_eq(gamePos.y, 50.f),
           "setEditorCamera does not overwrite authoritative game camera");

    commit_presentation_frame(renderer, viewport, &scene);
    const auto& editSnapshot = viewport.committed_snapshot();
    const auto editPick = editSnapshot.surface_to_world(SurfacePoint{ 0., 0. });
    expect(near_eq(static_cast<float>(editPick.x), 400.f)
           && near_eq(static_cast<float>(editPick.y), 300.f),
           "SceneEdit picking uses editor camera");

    viewport.set_presentation_mode(PresentationMode::CameraPreview);
    renderer.setWindowSize(1280, 720, "camera-modes-refresh");
    commit_presentation_frame(renderer, viewport, &scene);
    const auto& playSnapshot = viewport.committed_snapshot();
    const auto playPick = playSnapshot.surface_to_world(SurfacePoint{ 0., 0. });
    expect(near_eq(static_cast<float>(playPick.x), 100.f)
           && near_eq(static_cast<float>(playPick.y), 50.f),
           "CameraPreview picking uses game camera");

    renderer.setGameCameraModifiers({ 30., -20., 1., 0. });
    commit_presentation_frame(renderer, viewport, &scene);
    const auto pickWithShake = PresentationBindings::surface_to_world(
        viewport.committed_snapshot(), SurfacePoint{ 200., 300. });
    renderer.setGameCameraModifiers({});
    commit_presentation_frame(renderer, viewport, &scene);
    const auto pickNoShake = PresentationBindings::surface_to_world(
        viewport.committed_snapshot(), SurfacePoint{ 200., 300. });
    expect(!near_eq(static_cast<float>(pickWithShake.x), static_cast<float>(pickNoShake.x))
           || !near_eq(static_cast<float>(pickWithShake.y), static_cast<float>(pickNoShake.y)),
           "shake modifiers affect picking through the effective camera");

    renderer.setGameViewCompositorEnabled(true);
    viewport.set_presentation_mode(PresentationMode::PlayEmbedded);
    renderer.setWindowSize(1920, 1080, "play-embedded");
    renderer.setCameraPosition({ 0.f, 0.f });
    commit_presentation_frame(renderer, viewport, &scene);
    expect(viewport.presentation_mode() == PresentationMode::PlayEmbedded,
           "explicit PlayEmbedded mode is preserved");

    std::puts("presentation_camera_modes_test: all passed");
    return 0;
}
