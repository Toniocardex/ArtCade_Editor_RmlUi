#pragma once

#include "core/types.h"

#include <cmath>

namespace ArtCade::EditorNative::NumericValidation {

inline bool isFinite(float value) {
    return std::isfinite(value);
}

inline bool isFinite(const Vec2& value) {
    return isFinite(value.x) && isFinite(value.y);
}

inline bool isFinite(const Vec3& value) {
    return isFinite(value.x) && isFinite(value.y) && isFinite(value.z);
}

inline bool isFinite(const Vec4& value) {
    return isFinite(value.r) && isFinite(value.g)
        && isFinite(value.b) && isFinite(value.a);
}

inline bool isFinite(const Transform& transform) {
    return isFinite(transform.position) && isFinite(transform.scale)
        && isFinite(transform.rotation) && isFinite(transform.velocity);
}

inline bool isPositive(const Vec2& value) {
    return isFinite(value) && value.x > 0.f && value.y > 0.f;
}

inline bool isPositive(float value) {
    return isFinite(value) && value > 0.f;
}

inline bool isNonNegative(float value) {
    return isFinite(value) && value >= 0.f;
}

inline bool isValid(const SpriteAnimatorComponent& component) {
    return isPositive(component.playbackSpeed);
}

inline bool isValid(const SpritePresentationAnimation& source) {
    return isPositive(source.playbackSpeed);
}

inline bool isValid(const BoxCollider2DComponent& component) {
    return isFinite(component.offset) && isPositive(component.size);
}

inline bool isValid(const LinearMoverComponent& component) {
    return isFinite(Vec2{component.directionX, component.directionY})
        && isNonNegative(component.speed);
}

inline bool isValid(const AutoDestroyComponent& component) {
    return isNonNegative(component.lifespan);
}

inline bool isValid(const CameraTargetComponent& component) {
    return isFinite(component.offsetX) && isFinite(component.offsetY)
        && isNonNegative(component.followSpeed);
}

inline bool isValid(const TopDownControllerComponent& component) {
    return isNonNegative(component.maxSpeed)
        && isNonNegative(component.acceleration)
        && isNonNegative(component.friction);
}

inline bool isValid(const PlatformerControllerComponent& component) {
    return isNonNegative(component.maxSpeed)
        && isNonNegative(component.jumpForce)
        && isNonNegative(component.customGravity)
        && isNonNegative(component.coyoteTime)
        && isNonNegative(component.jumpBuffer)
        && isNonNegative(component.climbSpeed);
}

inline bool isValid(const TilemapComponent& component) {
    return isPositive(component.cellSize);
}

} // namespace ArtCade::EditorNative::NumericValidation
