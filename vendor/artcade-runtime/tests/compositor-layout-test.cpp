// compositor-layout-test.cpp — output policy placement (Phase 1 presentation module).

#include "../src/modules/presentation/include/output_policy.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using ArtCade::Presentation::output_placement_compute;
using ArtCade::OutputPolicy;

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
    const auto fit = output_placement_compute(
        1920., 1080., 512., 320., OutputPolicy::Fit);
    expect(fit.destW <= 1920. && fit.destH <= 1080., "fit dest fits backbuffer");
    expect(near_eq(fit.destW / fit.destH, 512. / 320.), "fit preserves viewport aspect");
    expect(near_eq(fit.scaleX, fit.scaleY), "fit uses uniform scale");

    const auto fill = output_placement_compute(
        1920., 1080., 512., 320., OutputPolicy::Fill);
    expect(near_eq(fill.destW, 1920.) && near_eq(fill.destH, 1080.), "fill covers backbuffer");
    expect(near_eq(fill.scaleX, fill.scaleY), "fill uses uniform scale");
    expect(fill.srcW < 512. || fill.srcH < 320., "fill crops game-view source");

    const auto stretch = output_placement_compute(
        1920., 1080., 512., 320., OutputPolicy::Stretch);
    expect(near_eq(stretch.destW, 1920.) && near_eq(stretch.destH, 1080.),
           "stretch covers backbuffer");
    expect(!near_eq(stretch.scaleX, stretch.scaleY), "stretch uses non-uniform scale");

    const auto same = output_placement_compute(
        512., 320., 512., 320., OutputPolicy::Fill);
    expect(near_eq(same.destW, 512.) && near_eq(same.destH, 320.),
           "equal sizes fill the full backbuffer");
    expect(near_eq(same.scaleX, 1.) && near_eq(same.scaleY, 1.),
           "equal sizes use unit scale");

    const auto letterbox = output_placement_compute(
        1920., 1080., 320., 240., OutputPolicy::Fit);
    expect(near_eq(letterbox.scaleX, 4.) && near_eq(letterbox.scaleY, 4.),
           "fit integer scale 4x for 320x240 on 1920x1080");
    expect(near_eq(letterbox.destW, 1280.) && near_eq(letterbox.destH, 960.),
           "fit content size 1280x960");
    expect(near_eq(letterbox.destX, 320.) && near_eq(letterbox.destY, 60.),
           "fit letterbox offset (320, 60)");

    std::puts("compositor_layout_test: all passed");
    return 0;
}
