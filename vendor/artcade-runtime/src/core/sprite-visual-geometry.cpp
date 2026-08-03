#include "sprite-visual-geometry.h"

#include <cmath>

namespace ArtCade {
namespace {

Vec2 rotateLocal(Vec2 local, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {local.x * c - local.y * s, local.x * s + local.y * c};
}

} // namespace

SpriteVisualGeometry resolveSpriteVisualGeometry(
    Vec2 anchorWorld,
    float rotationRadians,
    Vec2 scale,
    Vec2 unscaledFrameSize,
    Vec2 pivot,
    bool flipX,
    bool flipY) {
    SpriteVisualGeometry g;
    g.anchorWorld = anchorWorld;
    g.rotationRadians = rotationRadians;
    g.size = {
        unscaledFrameSize.x * std::abs(scale.x),
        unscaledFrameSize.y * std::abs(scale.y),
    };
    g.effectivePivot = {
        flipX ? 1.f - pivot.x : pivot.x,
        flipY ? 1.f - pivot.y : pivot.y,
    };
    g.originPixels = {
        g.effectivePivot.x * g.size.x,
        g.effectivePivot.y * g.size.y,
    };
    g.unrotatedTopLeft = {
        anchorWorld.x - g.originPixels.x,
        anchorWorld.y - g.originPixels.y,
    };
    const Vec2 centerOffsetLocal = {
        g.size.x * 0.5f - g.originPixels.x,
        g.size.y * 0.5f - g.originPixels.y,
    };
    const Vec2 rotated = rotateLocal(centerOffsetLocal, rotationRadians);
    g.visualCenter = {anchorWorld.x + rotated.x, anchorWorld.y + rotated.y};
    return g;
}

} // namespace ArtCade
