#pragma once

#include "types.h"

#include <algorithm>

namespace ArtCade::SpriteDrawMath {

inline float clamp01(float v) {
    return std::clamp(v, 0.f, 1.f);
}

inline Vec2 clampPivot(Vec2 p) {
    return { clamp01(p.x), clamp01(p.y) };
}

/** Raylib DrawTexturePro origin from normalised pivot and destination size. */
inline Vec2 drawOrigin(Vec2 pivot, float dstW, float dstH) {
    const Vec2 c = clampPivot(pivot);
    return { dstW * c.x, dstH * c.y };
}

/** Top-left of a placeholder rect when `pos` is the pivot anchor. */
inline Vec2 placeholderTopLeft(Vec2 pos, Vec2 pivot, float fw, float fh) {
    const Vec2 c = clampPivot(pivot);
    return { pos.x - fw * c.x, pos.y - fh * c.y };
}

} // namespace ArtCade::SpriteDrawMath
