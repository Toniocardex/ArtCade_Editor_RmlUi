#include "world/include/world.h"

#include <cmath>
#include <iostream>
#include <limits>

using namespace ArtCade;

static int passed = 0;
static int failed = 0;
#define CHECK(x) do { if (x) ++passed; else { ++failed; std::cerr << "FAIL " #x " line " << __LINE__ << "\n"; } } while (0)

int main() {
    // Viewport fits inside world: clamp to half..world-half
    {
        const Vec2 world{1024.f, 320.f};
        const Vec2 viewport{512.f, 320.f};
        const Vec2 left = clampCameraCenter(world, viewport, {-100.f, 160.f});
        CHECK(left.x == 256.f);
        CHECK(left.y == 160.f);
        const Vec2 right = clampCameraCenter(world, viewport, {2000.f, 160.f});
        CHECK(right.x == 768.f);
        const Vec2 mid = clampCameraCenter(world, viewport, {512.f, 160.f});
        CHECK(mid.x == 512.f && mid.y == 160.f);
    }

    // World == viewport → always world centre
    {
        const Vec2 world{512.f, 320.f};
        const Vec2 viewport{512.f, 320.f};
        const Vec2 c = clampCameraCenter(world, viewport, {0.f, 0.f});
        CHECK(c.x == 256.f && c.y == 160.f);
    }

    // Viewport larger on one axis → centre that axis
    {
        const Vec2 world{400.f, 320.f};
        const Vec2 viewport{512.f, 320.f};
        const Vec2 c = clampCameraCenter(world, viewport, {10.f, 10.f});
        CHECK(c.x == 200.f);
        CHECK(c.y == 160.f);
    }

    // Degenerate / non-finite extents
    {
        CHECK(clampCameraAxis(0.f, 100.f, 50.f) == 0.f);
        CHECK(clampCameraAxis(100.f, -1.f, 50.f) == 50.f);
        const float nan = std::numeric_limits<float>::quiet_NaN();
        CHECK(clampCameraAxis(100.f, 50.f, nan) == 50.f);
    }

    std::cout << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}
