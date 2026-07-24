// presentation-system-test.cpp — atomic committed snapshot per frame (Phase 2).

#include "../src/modules/presentation/include/presentation_system.h"
#include "../src/modules/presentation/include/surface_metrics.h"

#include <cmath>
#include <cstdlib>

using ArtCade::Presentation::PresentationMode;
using ArtCade::Presentation::PresentationSnapshot;
using ArtCade::Presentation::PresentationState;
using ArtCade::Presentation::PresentationSystem;
using ArtCade::Presentation::SurfacePoint;
using ArtCade::Presentation::WorldPoint;
using ArtCade::Presentation::surface_metrics_from_css;
using ArtCade::OutputPolicy;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

static PresentationState sample_state() {
    PresentationState state{};
    state.mode = PresentationMode::PlayEmbedded;
    state.outputPolicy = OutputPolicy::Fit;
    state.surface = surface_metrics_from_css(1920., 1080., 1.);
    state.logicalWidth = 320.;
    state.logicalHeight = 240.;
    state.worldWidth = 1280.;
    state.worldHeight = 720.;
    state.gameViewCompositorEnabled = true;
    state.gameCamera.positionX = 640.;
    state.gameCamera.positionY = 360.;
    state.gameCamera.zoom = 1.;
    return state;
}

int main() {
    PresentationSystem system;
    system.mutable_state() = sample_state();
    system.refresh_pending_snapshot();
    expect(system.committed_snapshot().revision == 0,
           "refresh_pending_snapshot does not commit");
    expect(system.pending_snapshot().revision == 1,
           "pending snapshot prepares the next revision");
    expect(system.pending_snapshot().placement.destW > 0.,
           "solver computes placement from raw inputs");

    system.begin_frame();
    const uint64_t revisionA = system.committed_snapshot().revision;
    expect(revisionA == 1, "begin_frame commits first revision");

    system.begin_frame();
    const uint64_t revisionB = system.committed_snapshot().revision;
    expect(revisionB > revisionA, "begin_frame keeps monotonic revisions");

    const PresentationSnapshot* found = system.find_snapshot(revisionA);
    expect(found != nullptr && found->revision == revisionA,
           "revision history retains prior snapshot");

    const WorldPoint world = system.committed_snapshot().surface_to_world(
        SurfacePoint{ 500., 360. });
    const SurfacePoint back = system.committed_snapshot().world_to_surface(world);
    expect(std::fabs(back.x - 500.) < 1e-5 && std::fabs(back.y - 360.) < 1e-5,
           "committed snapshot round-trip");

    std::puts("presentation_system_test: all passed");
    return 0;
}
