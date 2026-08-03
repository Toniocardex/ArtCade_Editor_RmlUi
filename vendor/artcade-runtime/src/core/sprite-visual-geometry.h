#pragma once
// ADR-0057 — pure sprite visual geometry (shared editor / runtime).
// Receives the unscaled frame size already chosen by the caller; pivot only
// moves the rectangle origin (no sprite-size redesign).

#include "types.h"

namespace ArtCade {

struct SpriteVisualGeometry {
    Vec2  anchorWorld{};
    Vec2  size{};
    Vec2  effectivePivot{};
    Vec2  originPixels{};
    Vec2  unrotatedTopLeft{};
    Vec2  visualCenter{};
    float rotationRadians = 0.f;
};

SpriteVisualGeometry resolveSpriteVisualGeometry(
    Vec2 anchorWorld,
    float rotationRadians,
    Vec2 scale,
    Vec2 unscaledFrameSize,
    Vec2 pivot,
    bool flipX,
    bool flipY);

} // namespace ArtCade
