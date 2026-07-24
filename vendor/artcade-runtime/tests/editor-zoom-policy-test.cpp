// editor-zoom-policy-test.cpp — editor zoom clamp parity with TS reducer.

#include "../src/modules/presentation/include/editor_zoom_policy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Presentation::editor_zoom_clamp;
using ArtCade::Presentation::kEditorZoomMax;
using ArtCade::Presentation::kEditorZoomMin;

static void expect(bool ok, const char* msg) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
    std::printf("  [ok] %s\n", msg);
}

int main() {
    expect(std::fabs(editor_zoom_clamp(0.05) - kEditorZoomMin) < 1e-9,
           "clamps below minimum");
    expect(std::fabs(editor_zoom_clamp(99.) - kEditorZoomMax) < 1e-9,
           "clamps above maximum");
    expect(std::fabs(editor_zoom_clamp(1.23456789) - 1.235) < 1e-9,
           "snaps to three decimals");

    std::puts("editor_zoom_policy_test: all passed");
    return 0;
}
