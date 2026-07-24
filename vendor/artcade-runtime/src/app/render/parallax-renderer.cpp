#include "parallax-renderer.h"

#include "../../modules/renderer/include/renderer.h"

#include <cmath>

namespace ArtCade {
namespace ParallaxRenderer {

namespace {

/** Positive floating-point modulo (result in [0, m) for m > 0). */
float posMod(float a, float m) {
    if (m <= 0.f) return 0.f;
    const float r = std::fmod(a, m);
    return r < 0.f ? r + m : r;
}

} // namespace

void draw(Modules::Renderer& renderer,
          const std::vector<SceneLayerDef>& layerStack,
          const std::unordered_map<std::string, SceneLayerSettings>& layerSettings,
          const Vec2& cameraTopLeft,
          const Vec2& viewSize,
          float elapsed)
{
    // Back-to-front: SceneDef.layers index 0 = background, last = foreground.
    for (const auto& layer : layerStack) {
        SceneLayerSettings settings;
        const auto sit = layerSettings.find(layer.id);
        if (sit != layerSettings.end()) settings = sit->second;
        if (!settings.visible || settings.opacity <= 0.f) continue;
        const LayerBackground& bg = settings.background;
        if (bg.imageId.empty()) continue;

        const Vec2 tex = renderer.spriteDestinationSize(bg.imageId, { 1.f, 1.f });
        if (tex.x <= 0.f || tex.y <= 0.f) continue;

        const float fx = settings.parallax.x;
        const float fy = settings.parallax.y;

        // World position of the first tile's top-left + how many tiles cover
        // the view on each axis. Tiled axes wrap with a parallax/scroll phase;
        // non-tiled axes draw a single copy that still scrolls with parallax.
        float originX, originY;
        int   countX, countY;

        if (bg.tileX) {
            const float phaseX = posMod(fx * cameraTopLeft.x + bg.scrollX * elapsed, tex.x);
            originX = cameraTopLeft.x - phaseX;
            countX  = static_cast<int>(std::ceil(viewSize.x / tex.x)) + 1;
        } else {
            originX = cameraTopLeft.x * (1.f - fx) - bg.scrollX * elapsed;
            countX  = 1;
        }

        if (bg.tileY) {
            const float phaseY = posMod(fy * cameraTopLeft.y + bg.scrollY * elapsed, tex.y);
            originY = cameraTopLeft.y - phaseY;
            countY  = static_cast<int>(std::ceil(viewSize.y / tex.y)) + 1;
        } else {
            originY = cameraTopLeft.y * (1.f - fy) - bg.scrollY * elapsed;
            countY  = 1;
        }

        for (int j = 0; j < countY; ++j) {
            const float wy = originY + static_cast<float>(j) * tex.y;
            for (int i = 0; i < countX; ++i) {
                const float wx = originX + static_cast<float>(i) * tex.x;
                renderer.drawSpriteRegion(bg.imageId, 0.f, 0.f, tex.x, tex.y,
                                          wx, wy, tex.x, tex.y, settings.opacity);
            }
        }
    }
}

} // namespace ParallaxRenderer
} // namespace ArtCade
