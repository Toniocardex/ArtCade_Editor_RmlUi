#include "editor-native/model/authored_transform.h"

#include <algorithm>
#include <cstdio>
#include <string>

namespace ArtCade::EditorNative {

SceneFrameTransform2D projectTransform(const Transform& transform, Vec2 unscaledSize) {
    SceneFrameTransform2D out;
    out.center = transform.position;
    out.size = Vec2{unscaledSize.x * transform.scale.x,
                    unscaledSize.y * transform.scale.y};
    out.rotationRadians = transform.rotation;
    return out;
}

TransformAabb aabbOfTransform(const SceneFrameTransform2D& transform) {
    const float hx = transform.size.x * 0.5f;
    const float hy = transform.size.y * 0.5f;
    const float c = std::cos(transform.rotationRadians);
    const float s = std::sin(transform.rotationRadians);
    const Vec2 corners[4] = {
        {transform.center.x + (-hx) * c - (-hy) * s,
         transform.center.y + (-hx) * s + (-hy) * c},
        {transform.center.x + ( hx) * c - (-hy) * s,
         transform.center.y + ( hx) * s + (-hy) * c},
        {transform.center.x + ( hx) * c - ( hy) * s,
         transform.center.y + ( hx) * s + ( hy) * c},
        {transform.center.x + (-hx) * c - ( hy) * s,
         transform.center.y + (-hx) * s + ( hy) * c},
    };
    float minX = corners[0].x, maxX = corners[0].x;
    float minY = corners[0].y, maxY = corners[0].y;
    for (int i = 1; i < 4; ++i) {
        minX = std::min(minX, corners[i].x);
        maxX = std::max(maxX, corners[i].x);
        minY = std::min(minY, corners[i].y);
        maxY = std::max(maxY, corners[i].y);
    }
    return TransformAabb{minX, minY, maxX - minX, maxY - minY};
}

bool transformContainsPoint(const SceneFrameTransform2D& transform, Vec2 worldPoint) {
    const float dx = worldPoint.x - transform.center.x;
    const float dy = worldPoint.y - transform.center.y;
    const float c = std::cos(-transform.rotationRadians);
    const float s = std::sin(-transform.rotationRadians);
    const float localX = dx * c - dy * s;
    const float localY = dx * s + dy * c;
    return std::fabs(localX) <= transform.size.x * 0.5f
        && std::fabs(localY) <= transform.size.y * 0.5f;
}

std::string formatAuthoringFloat(float value, int precision) {
    if (!std::isfinite(value)) return "0";
    if (precision < 0) precision = 0;
    if (precision > 6) precision = 6;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", precision, static_cast<double>(value));
    std::string text = buf;
    if (const auto dot = text.find('.'); dot != std::string::npos) {
        while (!text.empty() && text.back() == '0') text.pop_back();
        if (!text.empty() && text.back() == '.') text.pop_back();
    }
    if (text.empty() || text == "-") return "0";
    return text;
}

} // namespace ArtCade::EditorNative
