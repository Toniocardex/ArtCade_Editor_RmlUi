// presentation-integration-test.cpp — renderer + picking share committed snapshot (Phase 2).

#include "../src/modules/renderer/include/renderer.h"
#include "../src/modules/presentation/include/editor_viewport_service.h"
#include "../src/modules/presentation/include/presentation_bindings.h"
#include "../src/modules/presentation/include/presentation_mode.h"
#include "presentation_test_helpers.h"
#include "../src/core/types.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Modules::Renderer;
using ArtCade::Presentation::PresentationBindings;
using ArtCade::Presentation::EditorViewportService;
using ArtCade::Presentation::SurfacePoint;

static void commitPresentationFrame(
    Renderer& renderer,
    EditorViewportService& viewport) {
    commit_presentation_frame(renderer, viewport);
}

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
    renderer.setWindowSize(1280, 720, "integration-test");
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::CameraPreview);
    renderer.setCameraZoom(2.f);
    renderer.setCameraPosition({ 640.f, 360.f });

    commitPresentationFrame(renderer, viewport);
    const uint64_t revision = viewport.presentation_revision();
    expect(revision >= 1, "projection refresh exposes committed revision");

    const auto viaBindings = PresentationBindings::surface_to_world(
        viewport.committed_snapshot(),
        SurfacePoint{ 100., 200. });
    const auto& snapshot = viewport.committed_snapshot();
    expect(snapshot.revision == revision, "renderer and snapshot share revision");
    const auto viaSnapshot = snapshot.surface_to_world(SurfacePoint{ 100., 200. });
    expect(near_eq(static_cast<float>(viaBindings.x), static_cast<float>(viaSnapshot.x))
           && near_eq(static_cast<float>(viaBindings.y), static_cast<float>(viaSnapshot.y)),
           "PresentationBindings matches committed snapshot");

    const auto layout = renderer.compositorLayout();
    expect(near_eq(static_cast<float>(layout.scaleX),
                   static_cast<float>(snapshot.placement.scaleX)),
           "compositorLayout reads committed placement");

    renderer.setGameViewCompositorEnabled(true);
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::PlayEmbedded);
    renderer.setWindowSize(1920, 1080, "integration-fill");
    renderer.setOutputPolicy(ArtCade::OutputPolicy::Fill);
    commitPresentationFrame(renderer, viewport);
    const auto& playSnapshot = viewport.committed_snapshot();
    const auto playWorld = PresentationBindings::surface_to_world(
        playSnapshot, SurfacePoint{ 960., 540. });
    const auto playSnap = playSnapshot.surface_to_world(SurfacePoint{ 960., 540. });
    expect(near_eq(static_cast<float>(playWorld.x), static_cast<float>(playSnap.x))
           && near_eq(static_cast<float>(playWorld.y), static_cast<float>(playSnap.y)),
           "play compositor picking uses committed snapshot");

    std::puts("presentation_integration_test: all passed");
    return 0;
}
