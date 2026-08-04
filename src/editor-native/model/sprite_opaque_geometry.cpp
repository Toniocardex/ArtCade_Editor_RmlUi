#include "editor-native/model/sprite_opaque_geometry.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::EditorNative {

std::optional<SpritePixelRect> computeOpaqueSpritePixelBounds(
    const std::uint8_t* alpha,
    int imageWidth,
    int imageHeight,
    SpritePixelRect source) {
    if (!alpha || imageWidth <= 0 || imageHeight <= 0
        || source.width <= 0 || source.height <= 0) {
        return std::nullopt;
    }

    const int x0 = std::clamp(source.x, 0, imageWidth);
    const int y0 = std::clamp(source.y, 0, imageHeight);
    const int x1 = std::clamp(source.x + source.width, 0, imageWidth);
    const int y1 = std::clamp(source.y + source.height, 0, imageHeight);
    if (x1 <= x0 || y1 <= y0) return std::nullopt;

    int minX = x1;
    int minY = y1;
    int maxX = x0 - 1;
    int maxY = y0 - 1;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const std::size_t index = static_cast<std::size_t>(y)
                * static_cast<std::size_t>(imageWidth)
                + static_cast<std::size_t>(x);
            if (alpha[index] == 0) continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }
    if (maxX < minX || maxY < minY) return std::nullopt;
    return SpritePixelRect{minX, minY, maxX - minX + 1, maxY - minY + 1};
}

void applyOpaqueSpriteGeometry(SceneFrameSprite& sprite,
                               SpritePixelRect source,
                               SpritePixelRect opaque) {
    if (source.width <= 0 || source.height <= 0
        || opaque.width <= 0 || opaque.height <= 0
        || sprite.destination.width <= 0.f || sprite.destination.height <= 0.f) {
        return;
    }

    float u0 = static_cast<float>(opaque.x - source.x)
        / static_cast<float>(source.width);
    float v0 = static_cast<float>(opaque.y - source.y)
        / static_cast<float>(source.height);
    float u1 = static_cast<float>(opaque.x + opaque.width - source.x)
        / static_cast<float>(source.width);
    float v1 = static_cast<float>(opaque.y + opaque.height - source.y)
        / static_cast<float>(source.height);
    u0 = std::clamp(u0, 0.f, 1.f);
    v0 = std::clamp(v0, 0.f, 1.f);
    u1 = std::clamp(u1, 0.f, 1.f);
    v1 = std::clamp(v1, 0.f, 1.f);
    if (sprite.flipX) {
        const float nextU0 = 1.f - u1;
        u1 = 1.f - u0;
        u0 = nextU0;
    }
    if (sprite.flipY) {
        const float nextV0 = 1.f - v1;
        v1 = 1.f - v0;
        v0 = nextV0;
    }

    const Vec2 tightSize{
        (u1 - u0) * sprite.destination.width,
        (v1 - v0) * sprite.destination.height,
    };
    if (!(tightSize.x > 0.f) || !(tightSize.y > 0.f)) return;

    const Vec2 tightCenterFromFullTopLeft{
        (u0 + u1) * 0.5f * sprite.destination.width,
        (v0 + v1) * 0.5f * sprite.destination.height,
    };
    const Vec2 centerFromEntityOrigin{
        tightCenterFromFullTopLeft.x - sprite.origin.x,
        tightCenterFromFullTopLeft.y - sprite.origin.y,
    };
    const float c = std::cos(sprite.rotationRadians);
    const float s = std::sin(sprite.rotationRadians);
    const Vec2 entityOrigin{
        sprite.destination.x + sprite.origin.x,
        sprite.destination.y + sprite.origin.y,
    };
    sprite.visualTransform = SceneFrameTransform2D{
        {
            entityOrigin.x + centerFromEntityOrigin.x * c - centerFromEntityOrigin.y * s,
            entityOrigin.y + centerFromEntityOrigin.x * s + centerFromEntityOrigin.y * c,
        },
        tightSize,
        sprite.rotationRadians,
    };
    sprite.visualPivot = {
        (sprite.origin.x - u0 * sprite.destination.width) / tightSize.x,
        (sprite.origin.y - v0 * sprite.destination.height) / tightSize.y,
    };
}

} // namespace ArtCade::EditorNative
