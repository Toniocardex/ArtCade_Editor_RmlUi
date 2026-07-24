// frame-selection-test.cpp — world bounds for "F = frame selected".

#include "../src/modules/presentation/include/frame_selection.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Presentation::FRAME_SELECTION_SPAN;
using ArtCade::Presentation::frame_selection_world_bounds;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

int main() {
    const auto unit = frame_selection_world_bounds(240., 130., 1., 1.);
    expect(std::fabs(unit.minX - (240. - FRAME_SELECTION_SPAN * 0.5)) < 1e-9,
           "bounds centred on position X");
    expect(std::fabs(unit.maxY - (130. + FRAME_SELECTION_SPAN * 0.5)) < 1e-9,
           "bounds centred on position Y");

    const auto wide = frame_selection_world_bounds(0., 0., 4., 1.);
    const auto narrow = frame_selection_world_bounds(0., 0., 1., 1.);
    expect((wide.maxX - wide.minX) > (narrow.maxX - narrow.minX),
           "scale widens framed span on X");

    const auto capped = frame_selection_world_bounds(0., 0., 1000., 1000.);
    expect(std::fabs((capped.maxX - capped.minX)
                     - FRAME_SELECTION_SPAN * 8.) < 1e-9,
           "extreme scale is capped at 8x");

    std::puts("frame_selection_test: all passed");
    return 0;
}
