#pragma once

#include "core/types.h"

#include <cmath>
#include <optional>
#include <string>

namespace ArtCade::EditorNative {

// Partial authored Transform update. Not persisted — describes which fields a
// single SetEntityTransformCommand changes (position / rotation / scale only).
struct AuthoredTransformPatch {
    std::optional<Vec2> position;
    std::optional<float> rotationRadians;
    std::optional<Vec2> scale;
};

inline constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
inline constexpr float kRadToDeg = 180.f / 3.14159265358979323846f;
inline constexpr float kMinAuthoringScale = 0.001f;
inline constexpr float kTransformEpsilon = 0.00001f;

inline bool nearlyEqualTransform(float a, float b) {
    return std::fabs(a - b) <= kTransformEpsilon;
}

inline bool nearlyEqualTransform(const Vec2& a, const Vec2& b) {
    return nearlyEqualTransform(a.x, b.x) && nearlyEqualTransform(a.y, b.y);
}

// Visual projection of an authored Transform for placeholder / sprite / pick.
struct SceneFrameTransform2D {
    Vec2 center{};
    Vec2 size{32.f, 32.f};
    float rotationRadians = 0.f;
};

struct TransformAabb {
    float x = 0.f;
    float y = 0.f;
    float width = 0.f;
    float height = 0.f;
};

SceneFrameTransform2D projectTransform(const Transform& transform, Vec2 unscaledSize);
TransformAabb aabbOfTransform(const SceneFrameTransform2D& transform);
bool transformContainsPoint(const SceneFrameTransform2D& transform, Vec2 worldPoint);

std::string formatAuthoringFloat(float value, int precision = 3);

} // namespace ArtCade::EditorNative
