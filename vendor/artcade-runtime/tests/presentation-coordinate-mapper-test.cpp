// presentation-coordinate-mapper-test.cpp — golden round-trip and picking edge cases.

#include "../src/modules/presentation/include/coordinate_mapper.h"
#include "../src/modules/presentation/include/output_policy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Presentation::LogicalPoint;
using ArtCade::Presentation::OutputPlacement;
using ArtCade::Presentation::SurfacePoint;
using ArtCade::Presentation::ViewCamera2D;
using ArtCade::Presentation::WorldPoint;
using ArtCade::Presentation::output_placement_compute;
using ArtCade::OutputPolicy;

namespace {

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

bool near_eq(double a, double b, double eps = 1e-6) {
    return std::fabs(a - b) <= eps;
}

void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

void expect_round_trip_surface_logical(const OutputPlacement& placement,
                                       double sx, double sy) {
    const SurfacePoint surface{ sx, sy };
    const SurfacePoint back = surface_from_logical(
        logical_from_surface(surface, placement), placement);
    expect(near_eq(back.x, sx) && near_eq(back.y, sy), "surface/logical round-trip");
}

void expect_round_trip_world_surface(const OutputPlacement& placement,
                                     const ViewCamera2D& camera,
                                     double sx, double sy) {
    const SurfacePoint surface{ sx, sy };
    const WorldPoint world = world_from_surface(surface, placement, camera);
    const SurfacePoint back = surface_from_world(world, placement, camera);
    expect(near_eq(back.x, sx) && near_eq(back.y, sy), "surface/world round-trip");
}

} // namespace

int main() {
    const OutputPlacement placement = output_placement_compute(
        1920., 1080., 320., 240., OutputPolicy::Fit);

    expect_round_trip_surface_logical(placement, 500., 360.);
    expect_round_trip_surface_logical(placement, 320., 60.);
    expect_round_trip_surface_logical(placement, 100., 100.);

    const ViewCamera2D camera{
        0., 0.,
        0., 0.,
        1.,
    };
    expect_round_trip_world_surface(placement, camera, 500., 360.);
    expect_round_trip_world_surface(placement, camera, 320., 60.);

    const LogicalPoint viewportTopLeft = logical_from_surface(
        SurfacePoint{ 320., 60. }, placement);
    expect(near_eq(viewportTopLeft.x, 0.) && near_eq(viewportTopLeft.y, 0.),
           "letterbox offset maps to logical origin");

    const LogicalPoint viewportCenter = logical_from_surface(
        SurfacePoint{ 960., 540. }, placement);
    expect(near_eq(viewportCenter.x, 160.) && near_eq(viewportCenter.y, 120.),
           "viewport center maps to logical center");

    const ViewCamera2D zoomed{
        640., 360.,
        0., 0.,
        2.,
    };
    const WorldPoint world = world_from_surface(
        SurfacePoint{ 100., 200. },
        identity_surface_placement(1280., 720.),
        zoomed);
    expect(near_eq(world.x, (100. / 2.) + 640.) && near_eq(world.y, (200. / 2.) + 360.),
           "world_from_surface matches camera zoom math");

    std::puts("presentation_coordinate_mapper_test: all passed");
    return 0;
}
