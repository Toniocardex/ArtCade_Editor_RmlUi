// parallax-projection-test.cpp — ADR-0056 pure projection math.

#include "app/render/parallax-renderer.h"

#include <cmath>
#include <iostream>

using namespace ArtCade;

static int g_passed = 0;
static int g_failed = 0;

#define CHECK(cond) \
    do { \
        if (cond) ++g_passed; \
        else { std::cerr << "FAIL: " #cond " (line " << __LINE__ << ")\n"; ++g_failed; } \
    } while (0)

#define CHECK_NEAR(a, b, eps) \
    do { \
        const float _a = (a); \
        const float _b = (b); \
        if (std::fabs(_a - _b) <= (eps)) ++g_passed; \
        else { \
            std::cerr << "FAIL: CHECK_NEAR(" #a ", " #b ") got " << _a \
                      << " vs " << _b << " (line " << __LINE__ << ")\n"; \
            ++g_failed; \
        } \
    } while (0)

int main() {
    const Vec2 world{100.f, 200.f};
    const Vec2 camera{40.f, 80.f};

    {
        const Vec2 p = ParallaxRenderer::parallaxWorldPosition(
            world, camera, LayerParallax{1.f, 1.f});
        CHECK_NEAR(p.x, world.x, 1e-5f);
        CHECK_NEAR(p.y, world.y, 1e-5f);
    }
    {
        // Factor 0 → fixed to camera (projectedWorld = W + C).
        const Vec2 p = ParallaxRenderer::parallaxWorldPosition(
            world, camera, LayerParallax{0.f, 0.f});
        CHECK_NEAR(p.x, world.x + camera.x, 1e-5f);
        CHECK_NEAR(p.y, world.y + camera.y, 1e-5f);
    }
    {
        const Vec2 p = ParallaxRenderer::parallaxWorldPosition(
            world, camera, LayerParallax{0.5f, 1.f});
        CHECK_NEAR(p.x, world.x + camera.x * 0.5f, 1e-5f);
        CHECK_NEAR(p.y, world.y, 1e-5f);
    }
    {
        const Vec2 p = ParallaxRenderer::parallaxWorldPosition(
            world, camera, LayerParallax{1.f, 0.25f});
        CHECK_NEAR(p.x, world.x, 1e-5f);
        CHECK_NEAR(p.y, world.y + camera.y * 0.75f, 1e-5f);
    }
    {
        // Foreground factor > 1 moves faster (negative camera contribution).
        const Vec2 p = ParallaxRenderer::parallaxWorldPosition(
            world, camera, LayerParallax{1.5f, 1.5f});
        CHECK_NEAR(p.x, world.x + camera.x * (1.f - 1.5f), 1e-5f);
        CHECK_NEAR(p.y, world.y + camera.y * (1.f - 1.5f), 1e-5f);
    }

    {
        // Screen-space text/gauge ignore projection; world-space use it.
        const Vec2 projected = ParallaxRenderer::parallaxWorldPosition(
            world, camera, LayerParallax{0.25f, 0.5f});
        CHECK(projected.x != world.x || projected.y != world.y);
        const Vec2 hud = ParallaxRenderer::parallaxAwareUiBasePosition(
            world, projected, /*screenSpace=*/true);
        CHECK_NEAR(hud.x, world.x, 1e-5f);
        CHECK_NEAR(hud.y, world.y, 1e-5f);
        const Vec2 worldUi = ParallaxRenderer::parallaxAwareUiBasePosition(
            world, projected, /*screenSpace=*/false);
        CHECK_NEAR(worldUi.x, projected.x, 1e-5f);
        CHECK_NEAR(worldUi.y, projected.y, 1e-5f);
    }

    if (g_failed == 0) {
        std::cout << "parallax-projection-test: " << g_passed << " passed\n";
        return 0;
    }
    std::cerr << "parallax-projection-test: " << g_passed << " passed, "
              << g_failed << " failed\n";
    return 1;
}
