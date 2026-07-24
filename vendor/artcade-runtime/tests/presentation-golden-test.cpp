// presentation-golden-test.cpp — ADR §Required tests (Phase 1 checklist).

#include "../src/modules/presentation/include/coordinate_mapper.h"
#include "../src/modules/presentation/include/output_policy.h"
#include "../src/modules/presentation/include/presentation_mode.h"
#include "../src/modules/presentation/include/presentation_solver.h"
#include "../src/modules/presentation/include/presentation_state.h"
#include "../src/modules/presentation/include/surface_metrics.h"
#include "../src/modules/presentation/include/view_controller.h"
#include "../src/modules/renderer/include/compositor-layout.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Presentation::CssPoint;
using ArtCade::Presentation::EditorViewState;
using ArtCade::Presentation::LogicalPoint;
using ArtCade::Presentation::OutputPlacement;
using ArtCade::Presentation::PresentationMode;
using ArtCade::Presentation::PresentationSnapshot;
using ArtCade::Presentation::PresentationState;
using ArtCade::Presentation::SurfaceMetrics;
using ArtCade::Presentation::SurfacePoint;
using ArtCade::Presentation::ViewCamera2D;
using ArtCade::Presentation::ViewController;
using ArtCade::Presentation::WorldPoint;
using ArtCade::Presentation::output_placement_compute;
using ArtCade::Presentation::solve_presentation_snapshot;
using ArtCade::Presentation::surface_metrics_from_css;
using ArtCade::Presentation::surface_point_from_css;
using ArtCade::OutputPolicy;

namespace {

bool near_eq(double a, double b, double eps = 1e-5) {
    return std::fabs(a - b) <= eps;
}

void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

OutputPlacement identity_surface_placement(double surfaceW, double surfaceH) {
    OutputPlacement placement{};
    placement.destW = surfaceW;
    placement.destH = surfaceH;
    placement.srcW = surfaceW;
    placement.srcH = surfaceH;
    placement.scaleX = 1.;
    placement.scaleY = 1.;
    return placement;
}

void test_round_trip() {
    const OutputPlacement placement = output_placement_compute(
        1920., 1080., 320., 240., OutputPolicy::Fit);
    const ViewCamera2D camera{ 40., 20., 8., 12., 1.75 };

    const SurfacePoint samples[] = {
        { 320., 60. },
        { 960., 540. },
        { 50., 50. },
        { 1900., 1000. },
    };
    for (const SurfacePoint& surface : samples) {
        const WorldPoint world = world_from_surface(surface, placement, camera);
        const SurfacePoint back = surface_from_world(world, placement, camera);
        expect(near_eq(back.x, surface.x) && near_eq(back.y, surface.y),
               "round-trip surfaceToWorld(worldToSurface(p))");
    }
}

void test_letterbox() {
    const OutputPlacement placement = output_placement_compute(
        1920., 1080., 320., 240., OutputPolicy::Fit);
    expect(near_eq(placement.scaleX, 4.) && near_eq(placement.scaleY, 4.),
           "letterbox integer fit 4x");
    expect(near_eq(placement.destW, 1280.) && near_eq(placement.destH, 960.),
           "letterbox content 1280x960");
    expect(near_eq(placement.destX, 320.) && near_eq(placement.destY, 60.),
           "letterbox offset (320, 60)");
}

void test_high_dpi() {
    const SurfaceMetrics metrics = surface_metrics_from_css(800., 600., 1.5);
    expect(near_eq(metrics.framebufferWidth, 1200.)
           && near_eq(metrics.framebufferHeight, 900.),
           "high DPI framebuffer 1200x900");

    const SurfacePoint fb = surface_point_from_css(CssPoint{ 400., 300. }, metrics);
    expect(near_eq(fb.x, 600.) && near_eq(fb.y, 450.),
           "CSS pointer maps to framebuffer via DPR scale");
}

void test_zoom_at_cursor() {
    ViewController controller;
    controller.resize_surface(1280., 720., 1.);
    EditorViewState view{};
    view.positionX = 100.;
    view.positionY = 50.;
    view.zoom = 2.;
    controller.set_editor_view(view);

    const SurfacePoint cursor{ 640., 360. };
    const OutputPlacement placement = identity_surface_placement(1280., 720.);
    const ViewCamera2D before{
        view.positionX, view.positionY, 0., 0., view.zoom,
    };
    const WorldPoint anchor = world_from_surface(cursor, placement, before);

    controller.zoom_at(cursor, 1.5);
    const EditorViewState after = controller.editor_view();
    const ViewCamera2D afterCamera{
        after.positionX, after.positionY, 0., 0., after.zoom,
    };
    const WorldPoint anchored = world_from_surface(cursor, placement, afterCamera);
    expect(near_eq(anchored.x, anchor.x) && near_eq(anchored.y, anchor.y),
           "zoom-at-cursor keeps world point under cursor");
}

void test_resize_preserves_editor_camera() {
    ViewController controller;
    controller.resize_surface(800., 600., 1.);
    EditorViewState view{};
    view.positionX = 42.;
    view.positionY = 17.;
    view.zoom = 1.25;
    controller.set_editor_view(view);

    controller.resize_surface(1024., 768., 1.25);
    const EditorViewState after = controller.editor_view();
    expect(near_eq(after.positionX, 42.) && near_eq(after.positionY, 17.)
           && near_eq(after.zoom, 1.25),
           "resize surface preserves editor camera world state");
}

void test_picking_edges() {
    const OutputPlacement placement = output_placement_compute(
        1000., 720., 320., 240., OutputPolicy::Fit);

    const LogicalPoint inside = logical_from_surface(
        SurfacePoint{ 500., 360. }, placement);
    expect(inside.x >= 0. && inside.y >= 0.
           && inside.x <= placement.srcW && inside.y <= placement.srcH,
           "picking inside viewport maps to logical interior");

    const LogicalPoint letterbox = logical_from_surface(
        SurfacePoint{ 10., 360. }, placement);
    expect(letterbox.x < 0., "picking on letterbox maps outside logical viewport");

    const ViewCamera2D fractional{
        0., 0., 0., 0., 1.33,
    };
    const SurfacePoint border{ placement.destX, placement.destY };
    const WorldPoint world = world_from_surface(border, placement, fractional);
    const SurfacePoint back = surface_from_world(world, placement, fractional);
    expect(near_eq(back.x, border.x) && near_eq(back.y, border.y),
           "picking on viewport border round-trips with non-integer zoom");

    const LogicalPoint negative = logical_from_surface(
        SurfacePoint{ -20., -10. }, placement);
    expect(negative.x < 0. && negative.y < 0.,
           "negative surface coords map outside logical viewport");
}

void test_legacy_layout_parity() {
    const float inputs[][4] = {
        { 1920.f, 1080.f, 320.f, 240.f },
        { 512.f, 320.f, 512.f, 288.f },
        { 1000.f, 720.f, 320.f, 240.f },
    };
    const OutputPolicy policies[] = {
        OutputPolicy::Fit,
        OutputPolicy::Fill,
        OutputPolicy::Stretch,
    };
    for (const auto& row : inputs) {
        for (OutputPolicy policy : policies) {
            const auto legacy = ArtCade::Modules::compositor_layout(
                row[0], row[1], row[2], row[3], policy);
            const auto current = output_placement_compute(
                row[0], row[1], row[2], row[3], policy);
            expect(near_eq(legacy.destX, current.destX)
                   && near_eq(legacy.destW, current.destW)
                   && near_eq(legacy.scaleX, current.scaleX)
                   && near_eq(legacy.srcX, current.srcX),
                   "native double path matches legacy float compositor_layout");
        }
    }
}

PresentationState scene_edit_state(double cssW, double cssH, double dpr) {
    PresentationState state{};
    state.mode = PresentationMode::SceneEdit;
    state.surface = surface_metrics_from_css(cssW, cssH, dpr);
    state.logicalWidth = cssW;
    state.logicalHeight = cssH;
    state.gameViewCompositorEnabled = false;
    state.editorCamera.positionX = 100.;
    state.editorCamera.positionY = 50.;
    state.editorCamera.zoom = 2.;
    return state;
}

void test_solver_scene_edit_snapshot_fields() {
    const PresentationSnapshot snapshot =
        solve_presentation_snapshot(scene_edit_state(800., 600., 2.), 7u);
    expect(snapshot.revision == 7u, "solver copies revision");
    expect(snapshot.effectiveMode == PresentationMode::SceneEdit,
           "solver preserves SceneEdit mode");
    expect(near_eq(snapshot.editorViewOriginX, 100.)
           && near_eq(snapshot.editorViewOriginY, 50.),
           "solver exposes editor view origin");
    expect(near_eq(snapshot.surfacePixelsPerWorldUnit, 2.),
           "solver exposes surface pixels per world unit");
    expect(snapshot.visibleWorldMaxX > snapshot.visibleWorldMinX
           && snapshot.visibleWorldMaxY > snapshot.visibleWorldMinY,
           "solver fills visible world bounds");
}

void test_solver_dpr_matrix_no_drift() {
    const double dprs[] = { 1., 1.25, 1.5, 2. };
    for (double dpr : dprs) {
        const PresentationSnapshot snapshot =
            solve_presentation_snapshot(scene_edit_state(800., 600., dpr), 1u);
        const SurfacePoint sample{ snapshot.surface.framebufferWidth * 0.5,
                                   snapshot.surface.framebufferHeight * 0.5 };
        const WorldPoint world = snapshot.surface_to_world(sample);
        const SurfacePoint back = snapshot.world_to_surface(world);
        expect(near_eq(back.x, sample.x) && near_eq(back.y, sample.y),
               "solver snapshot round-trip at DPR sample");
    }
}

void test_solver_picking_matches_revision() {
    const PresentationSnapshot snapshot =
        solve_presentation_snapshot(scene_edit_state(1024., 768., 1.), 42u);
    const SurfacePoint cursor{ 512., 384. };
    const WorldPoint world = snapshot.surface_to_world(cursor);
    const SurfacePoint back = snapshot.world_to_surface(world);
    expect(near_eq(back.x, cursor.x) && near_eq(back.y, cursor.y),
           "committed snapshot picking round-trip");
    expect(snapshot.revision == 42u, "picking snapshot revision is stable");
}

void test_play_policy_matrix() {
    struct Case {
        double logicalW;
        double logicalH;
        double cssW;
        double cssH;
        double dpr;
        OutputPolicy policy;
    };
    const Case cases[] = {
        { 320., 240., 900., 600., 1., OutputPolicy::Fit },
        { 320., 240., 900., 600., 1.5, OutputPolicy::Fit },
        { 512., 320., 1280., 720., 1., OutputPolicy::Fit },
        { 320., 240., 1920., 1080., 1., OutputPolicy::Fit },
        { 512., 320., 1280., 720., 1., OutputPolicy::Fill },
        { 320., 240., 900., 600., 1.25, OutputPolicy::Stretch },
    };
    const ViewCamera2D camera{ 0., 0., 0., 0., 1. };
    for (const Case& c : cases) {
        const SurfaceMetrics metrics = surface_metrics_from_css(c.cssW, c.cssH, c.dpr);
        const OutputPlacement placement = output_placement_compute(
            metrics.framebufferWidth,
            metrics.framebufferHeight,
            c.logicalW,
            c.logicalH,
            c.policy);
        expect(placement.destW > 0. && placement.destH > 0.,
               "policy matrix content rect is non-empty");
        expect(placement.scaleX > 0. && placement.scaleY > 0.,
               "policy matrix scale is positive");
        expect(placement.destX >= 0. && placement.destY >= 0.,
               "policy matrix letterbox offset is non-negative");
        const SurfacePoint sample{
            placement.destX + placement.destW * 0.5,
            placement.destY + placement.destH * 0.5,
        };
        const WorldPoint world = world_from_surface(sample, placement, camera);
        const SurfacePoint back = surface_from_world(world, placement, camera);
        expect(near_eq(back.x, sample.x) && near_eq(back.y, sample.y),
               "policy matrix surface/world round-trip");
    }
}

} // namespace

int main() {
    std::puts("presentation_golden_test:");
    test_round_trip();
    test_letterbox();
    test_high_dpi();
    test_zoom_at_cursor();
    test_resize_preserves_editor_camera();
    test_picking_edges();
    test_legacy_layout_parity();
    test_solver_scene_edit_snapshot_fields();
    test_solver_dpr_matrix_no_drift();
    test_solver_picking_matches_revision();
    test_play_policy_matrix();
    std::puts("presentation_golden_test: all passed");
    return 0;
}
