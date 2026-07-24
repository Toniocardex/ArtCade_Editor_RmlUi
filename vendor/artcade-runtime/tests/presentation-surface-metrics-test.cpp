// presentation-surface-metrics-test.cpp — CSS to framebuffer DPI conversion.

#include "../src/modules/presentation/include/surface_metrics.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Presentation::SurfaceMetrics;
using ArtCade::Presentation::surface_metrics_from_css;

static bool near_eq(double a, double b, double eps = 0.01) {
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
    const SurfaceMetrics hd = surface_metrics_from_css(800., 600., 1.5);
    expect(near_eq(hd.cssWidth, 800.) && near_eq(hd.cssHeight, 600.), "css size preserved");
    expect(near_eq(hd.devicePixelRatio, 1.5), "dpr preserved");
    expect(near_eq(hd.framebufferWidth, 1200.) && near_eq(hd.framebufferHeight, 900.),
           "framebuffer = round(css * dpr)");

    const SurfaceMetrics unit = surface_metrics_from_css(512., 288., 1.);
    expect(near_eq(unit.framebufferWidth, 512.) && near_eq(unit.framebufferHeight, 288.),
           "unit dpr keeps 1:1 framebuffer");

    const SurfaceMetrics clamped = surface_metrics_from_css(0., 0., 0.);
    expect(clamped.framebufferWidth >= 1. && clamped.framebufferHeight >= 1.,
           "invalid dpr clamps to minimum 1px framebuffer");

    std::puts("presentation_surface_metrics_test: all passed");
    return 0;
}
