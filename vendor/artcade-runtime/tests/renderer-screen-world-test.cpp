// renderer-screen-world-test.cpp — committed snapshot picking + clip math (Phase 4).
// Uses raylib stub (no GPU window).

#include "../src/modules/renderer/include/renderer.h"
#include "../src/modules/renderer/include/compositor-layout.h"
#include "../src/modules/presentation/include/editor_viewport_service.h"
#include "../src/modules/presentation/include/presentation_bindings.h"
#include "../src/modules/presentation/include/presentation_types.h"
#include "presentation_test_helpers.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Modules::Renderer;
using ArtCade::Modules::ScreenClipRect;
using ArtCade::OutputPolicy;
using ArtCade::Presentation::PresentationBindings;
using ArtCade::Presentation::EditorViewportService;
using ArtCade::Presentation::SurfacePoint;

static ArtCade::SceneDef scene_with_sizes(const ArtCade::Vec2& world,
                                          const ArtCade::Vec2& logical) {
    ArtCade::SceneDef scene{};
    scene.worldSize = world;
    scene.viewportSize = logical;
    return scene;
}

static ArtCade::Vec2 pickWorld(Renderer& renderer,
                                 EditorViewportService& viewport,
                                 float x, float y,
                                 const ArtCade::SceneDef* scene) {
    if (scene) {
        renderer.commitFrameGeometry(scene->worldSize, scene->viewportSize);
    }
    commit_presentation_frame(renderer, viewport, scene);
    const auto world = PresentationBindings::surface_to_world(
        viewport.committed_snapshot(),
        SurfacePoint{ x, y });
    return { static_cast<float>(world.x), static_cast<float>(world.y) };
}

static bool near(float a, float b, float eps = 0.001f) {
    return std::fabs(a - b) <= eps;
}

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

static void expectClip(const ScreenClipRect& clip,
                       float x, float y, float w, float h,
                       const char* msg) {
    expect(near(clip.x, x) && near(clip.y, y)
           && near(clip.width, w) && near(clip.height, h), msg);
}

int main() {
    ArtCade::Presentation::EditorViewportService viewport;
    Renderer renderer;
    renderer.setWindowSize(1280, 720, "test");
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::CameraPreview);

    const ArtCade::Vec2 fullHd{ 1280.f, 720.f };
    const ArtCade::SceneDef hdScene = scene_with_sizes(fullHd, fullHd);
    renderer.commitFrameGeometry(fullHd, fullHd);
    renderer.setCameraZoom(2.f);
    renderer.setCameraPosition({ 640.f, 360.f });

    const auto world = pickWorld(renderer, viewport, 100.f, 200.f, &hdScene);
    // (screen - offset) / zoom + target; offset is 0 with top-left origin.
    expect(near(world.x, (100.f / 2.f) + 640.f), "snapshot picking X");
    expect(near(world.y, (200.f / 2.f) + 360.f), "snapshot picking Y");

    renderer.setCameraPosition({ 0.f, 0.f });
    renderer.setCameraZoom(1.f);
    const auto origin = pickWorld(renderer, viewport, 0.f, 0.f, &hdScene);
    expect(near(origin.x, 0.f) && near(origin.y, 0.f), "snapshot picking at origin");

    // 1:1 world/viewport clamps authoritative camera; shake must stay render-only.
    renderer.setCameraZoom(1.f);
    renderer.setCameraPosition({ 0.f, 0.f });
    commit_test_geometry(renderer, viewport, fullHd, fullHd, &hdScene);
    renderer.setCameraPosition({ 12.f, 8.f });
    const auto clamped = renderer.getCameraPosition();
    expect(near(clamped.x, 0.f) && near(clamped.y, 0.f),
           "setCameraPosition clamped at 1:1 viewport");
    renderer.setGameCameraModifiers({ 20., 15., 1., 0. });
    const auto still = renderer.getCameraPosition();
    expect(near(still.x, 0.f) && near(still.y, 0.f),
           "render shake offset does not mutate authoritative camera");
    renderer.setGameCameraModifiers({});

    const ArtCade::Vec2 wideWorld{ 2560.f, 1440.f };
    const ArtCade::SceneDef wideSceneDef = scene_with_sizes(wideWorld, fullHd);
    renderer.commitFrameGeometry(wideWorld, fullHd);
    renderer.setCameraCenter({ 1280.f, 720.f });
    const auto centered = pickWorld(renderer, viewport, 640.f, 360.f, &wideSceneDef);
    expect(near(centered.x, 1280.f) && near(centered.y, 720.f),
           "setCameraCenter places world point at viewport center");
    const auto cameraCenter = renderer.getCameraCenter();
    expect(near(cameraCenter.x, 1280.f) && near(cameraCenter.y, 720.f),
           "getCameraCenter returns visible world center");

    renderer.setWindowSize(720, 360, "test-small-world");
    const ArtCade::Vec2 smallWorld{ 640.f, 480.f };
    const ArtCade::Vec2 wideViewport{ 720.f, 360.f };
    const ArtCade::SceneDef smallSceneDef = scene_with_sizes(smallWorld, wideViewport);
    renderer.setCameraPosition({ 0.f, 0.f });
    const auto smallWorldCenter = pickWorld(renderer, viewport, 360.f, 180.f, &smallSceneDef);
    expect(near(smallWorldCenter.x, 320.f) && near(smallWorldCenter.y, 180.f),
           "small world is centered inside wider viewport");
    const auto smallScreenCenter = pickWorld(renderer, viewport, 360.f, 180.f, &smallSceneDef);
    expect(near(smallScreenCenter.x, 320.f) && near(smallScreenCenter.y, 180.f),
           "small world visual center matches picking at viewport center");
    const auto leftInset = pickWorld(renderer, viewport, 0.f, 180.f, &smallSceneDef);
    expect(leftInset.x < 0.f && near(leftInset.y, 180.f),
           "screen margin maps outside centered small world");

    renderer.setWindowSize(1000, 720, "test-letterbox");
    const ArtCade::Vec2 letterboxLogical{ 320.f, 240.f };
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::CameraPreview);
    renderer.setGameViewCompositorEnabled(false);
    const ArtCade::SceneDef letterboxSceneDef =
        scene_with_sizes(smallWorld, letterboxLogical);
    renderer.setCameraPosition({ 0.f, 0.f });
    const auto letterboxLeft = pickWorld(renderer, viewport, 0.f, 360.f, &letterboxSceneDef);
    expect(near(letterboxLeft.x, -20.f / 3.f) && near(letterboxLeft.y, 120.f),
           "letterbox margin maps outside the logical viewport");
    const auto viewportTopLeft = pickWorld(renderer, viewport, 20.f, 0.f, &letterboxSceneDef);
    expect(near(viewportTopLeft.x, 0.f) && near(viewportTopLeft.y, 0.f),
           "scaled viewport starts after the letterbox offset");
    const auto viewportCenter = pickWorld(renderer, viewport, 500.f, 360.f, &letterboxSceneDef);
    commit_test_geometry(renderer, viewport, smallWorld, letterboxLogical, &letterboxSceneDef);
    expect(near(viewportCenter.x, 160.f) && near(viewportCenter.y, 120.f),
           "scaled viewport center maps to logical center");
    const auto visible = renderer.visibleWorldSize();
    expect(near(visible.x, 320.f) && near(visible.y, 240.f),
           "physical window size does not change logical visible world");

    renderer.setGameCameraModifiers({});
    renderer.setGameViewCompositorEnabled(false);
    renderer.setWindowSize(1280, 720, "test-full-clip");
    renderer.setCameraPosition({ 0.f, 0.f });
    renderer.setCameraZoom(1.f);
    commit_test_geometry(renderer, viewport, fullHd, fullHd, &hdScene);
    expectClip(renderer.worldScreenClipRect(), 0.f, 0.f, 1280.f, 720.f,
               "world=viewport clip covers the full framebuffer");

    renderer.setWindowSize(512, 320, "test-16-9-clip");
    const ArtCade::Vec2 tallViewport{ 512.f, 320.f };
    const ArtCade::Vec2 wideScene{ 512.f, 288.f };
    const ArtCade::SceneDef tallSceneDef = scene_with_sizes(wideScene, tallViewport);
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::CameraPreview);
    renderer.setCameraPosition({ 0.f, 0.f });
    renderer.setCameraZoom(1.f);
    commit_test_geometry(renderer, viewport, wideScene, tallViewport, &tallSceneDef);
    expectClip(renderer.worldScreenClipRect(), 0.f, 16.f, 512.f, 288.f,
               "16:9 world inside taller viewport clips letterbox bands");

    renderer.setGameViewCompositorEnabled(true);
    renderer.setWindowSize(1000, 720, "test-gameview-clip");
    renderer.setCameraPosition({ 0.f, 0.f });
    renderer.setCameraZoom(1.f);
    commit_test_geometry(renderer, viewport, smallWorld, letterboxLogical, &letterboxSceneDef);
    expectClip(renderer.worldScreenClipRect(), 0.f, 0.f, 320.f, 240.f,
               "game-view compositor clip uses viewport-sized target space");

    renderer.setWindowSize(1920, 1080, "test-fill-compositor");
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::PlayEmbedded);
    renderer.setOutputPolicy(OutputPolicy::Fill);
    renderer.setCameraPosition({ 0.f, 0.f });
    renderer.setCameraZoom(1.f);
    const auto expectedFill = ArtCade::Modules::compositor_layout(
        1920.f, 1080.f, 320.f, 240.f, OutputPolicy::Fill);
    commit_test_geometry(renderer, viewport, smallWorld, letterboxLogical, &letterboxSceneDef);
    const auto layoutFill = renderer.compositorLayout();
    expect(near(layoutFill.destW, expectedFill.destW)
           && near(layoutFill.destH, expectedFill.destH),
           "fill compositor layout matches compositor_layout");
    expect(near(layoutFill.scaleX, expectedFill.scaleX)
           && near(layoutFill.scaleY, expectedFill.scaleY),
           "fill scale matches compositor_layout");

    renderer.setGameViewCompositorEnabled(false);
    renderer.setWindowSize(512, 320, "test-editor-camera");
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::SceneEdit);
    viewport.set_editor_camera(0., 0., 1.);
    const ArtCade::Vec2 editorWorld{ 512.f, 288.f };
    const ArtCade::SceneDef editorSceneDef = scene_with_sizes(editorWorld, editorWorld);
    renderer.setWindowSize(512, 320, "test-editor-camera-refresh");
    commit_test_geometry(renderer, viewport, editorWorld, editorWorld, &editorSceneDef);
    const auto gridOrigin = pickWorld(renderer, viewport, 0.f, 0.f, &editorSceneDef);
    const auto gridStep = pickWorld(renderer, viewport, 32.f, 32.f, &editorSceneDef);
    expect(near(gridOrigin.x, 0.f) && near(gridOrigin.y, 0.f),
           "editor camera keeps 1:1 origin after projection refresh");
    expect(near(gridStep.x, 32.f) && near(gridStep.y, 32.f),
           "editor camera keeps 1:1 grid spacing on tall framebuffer");

    renderer.setGameViewCompositorEnabled(true);
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::PlayEmbedded);
    renderer.resizePlayFramebuffer(900.f, 600.f, 1.5f);
    viewport.sync_play_surface(900., 600., 1.5, renderer.windowWidth(), renderer.windowHeight());
    commit_test_geometry(renderer, viewport, smallWorld, letterboxLogical, &letterboxSceneDef);
    const auto& dprSnapshot = viewport.committed_snapshot();
    expect(near(static_cast<float>(dprSnapshot.surface.cssWidth), 900.f)
           && near(static_cast<float>(dprSnapshot.surface.cssHeight), 600.f),
           "syncPlaySurface preserves CSS host size");
    expect(near(static_cast<float>(dprSnapshot.surface.framebufferWidth), 1350.f)
           && near(static_cast<float>(dprSnapshot.surface.framebufferHeight), 900.f),
           "syncPlaySurface rounds CSS times DPR into framebuffer size");
    expect(near(static_cast<float>(dprSnapshot.surface.devicePixelRatio), 1.5f),
           "syncPlaySurface preserves fractional DPR");

    // Geometry commits via beginFrame (no autonomous renderer store).
    renderer.setGameViewCompositorEnabled(false);
    renderer.setWindowSize(2048, 320, "test-frame-world-size");
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::SceneEdit);
    viewport.set_editor_camera(0., 0., 1.);
    const ArtCade::Vec2 narrowWorld{ 512.f, 320.f };
    const ArtCade::Vec2 wideLogical{ 2048.f, 320.f };
    renderer.setCameraPosition({ 0.f, 0.f });
    renderer.setCameraZoom(1.f);
    commit_test_geometry(renderer, viewport, narrowWorld, wideLogical);
    const float narrowClipW = renderer.worldScreenClipRect().width;
    expect(near(narrowClipW, 512.f), "512 world bounds yield 512px scissor at 1:1");
    commit_test_geometry(renderer, viewport, wideLogical, wideLogical);
    const float wideClipW = renderer.worldScreenClipRect().width;
    expect(near(wideClipW, 2048.f),
           "2048 world bounds widen scissor to framebuffer width");
    expect(wideClipW > narrowClipW, "wider world bounds widen scissor");
    commit_test_geometry(renderer, viewport, { 0.f, 0.f }, { 0.f, 0.f });
    expect(near(renderer.worldScreenClipRect().width, 512.f),
           "invalid frame geometry falls back to project defaults");

    renderer.setGameViewCompositorEnabled(false);
    renderer.setWindowSize(1272, 860, "centered-editor-clip");
    viewport.set_presentation_mode(ArtCade::Presentation::PresentationMode::SceneEdit);
    viewport.set_editor_camera(-380., -270., 1.);
    const ArtCade::Vec2 centeredWorld{ 512.f, 320.f };
    commit_test_geometry(renderer, viewport, centeredWorld, centeredWorld);
    const ScreenClipRect centeredClip = renderer.worldScreenClipRect();
    const auto& pick = viewport.committed_snapshot().pickingCamera;
    std::fprintf(stderr,
        "centered-clip: xy=%f,%f wh=%fx%f pick=%f,%f\n",
        centeredClip.x, centeredClip.y,
        centeredClip.width, centeredClip.height,
        pick.targetX, pick.targetY);
    expect(near(centeredClip.x, 380.f) && near(centeredClip.y, 270.f),
           "centered editor camera scissor origin");
    expect(near(centeredClip.width, 512.f) && near(centeredClip.height, 320.f),
           "centered editor camera scissor spans full scene");

    std::puts("renderer_screen_world_test: all passed");
    return 0;
}
