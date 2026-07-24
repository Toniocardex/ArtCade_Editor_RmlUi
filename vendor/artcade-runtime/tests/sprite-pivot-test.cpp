// sprite-pivot-test.cpp — pure pivot math for DrawTexturePro / placeholder rects.

#include "../src/core/sprite-draw-math.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace ArtCade::SpriteDrawMath;
using ArtCade::Vec2;

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

int main() {
    const Vec2 center = drawOrigin({ 0.5f, 0.5f }, 100.f, 80.f);
    expect(near(center.x, 50.f) && near(center.y, 40.f), "center origin");

    const Vec2 feet = drawOrigin({ 0.5f, 1.f }, 100.f, 80.f);
    expect(near(feet.x, 50.f) && near(feet.y, 80.f), "bottom-center origin");

    const Vec2 tl = placeholderTopLeft({ 200.f, 300.f }, { 0.f, 0.f }, 32.f, 32.f);
    expect(near(tl.x, 200.f) && near(tl.y, 300.f), "top-left placeholder");

    const Vec2 centerRect = placeholderTopLeft({ 100.f, 100.f }, { 0.5f, 0.5f }, 40.f, 20.f);
    expect(near(centerRect.x, 80.f) && near(centerRect.y, 90.f), "center placeholder");

    const Vec2 clamped = drawOrigin({ 2.f, -1.f }, 10.f, 10.f);
    expect(near(clamped.x, 10.f) && near(clamped.y, 0.f), "pivot clamped to 0..1");

    std::puts("sprite_pivot_test: all passed");
    return 0;
}
