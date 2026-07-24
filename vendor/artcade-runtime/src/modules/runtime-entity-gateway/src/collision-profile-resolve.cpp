#include "collision-profile-resolve.h"

#include "../../sprite-animator/include/sprite-animator.h"

#include <algorithm>
#include <cmath>

namespace ArtCade::Modules::CollisionProfileResolve {

namespace {

const CollisionProfileDef* find_profile(
    const CollisionBodyComponent& authored,
    const SpriteComponent& sprite,
    const std::unordered_map<std::string, CollisionProfileDef>& profiles,
    const std::unordered_map<std::string, std::string>& spritePathToAssetId)
{
    if (!authored.profileId.empty()) {
        auto it = profiles.find(authored.profileId);
        if (it != profiles.end()) return &it->second;
    }
    if (!sprite.spriteAssetId.empty()) {
        auto mapIt = spritePathToAssetId.find(sprite.spriteAssetId);
        if (mapIt != spritePathToAssetId.end()) {
            auto it = profiles.find(mapIt->second);
            if (it != profiles.end()) return &it->second;
        }
    }
    return nullptr;
}

CollisionShape shape_from_normalized(
    const CollisionShape& src,
    float frameW,
    float frameH,
    float scaleX,
    float scaleY,
    const Vec2& pivot)
{
    CollisionShape out = src;
    const float absSx = std::max(0.01f, std::abs(scaleX));
    const float absSy = std::max(0.01f, std::abs(scaleY));
    const float worldW = std::max(1.f, src.size.x * frameW * absSx);
    const float worldH = std::max(1.f, src.size.y * frameH * absSy);

    const float normCenterX = src.offset.x + src.size.x * 0.5f;
    const float normCenterY = src.offset.y + src.size.y * 0.5f;
    const float localCenterX = (normCenterX - pivot.x) * frameW * absSx;
    const float localCenterY = (normCenterY - pivot.y) * frameH * absSy;

    out.offset = { localCenterX, localCenterY };
    out.size = { worldW, worldH };
    if (out.type == CollisionShapeType::Polygon && !src.points.empty()) {
        out.offset = {};
        out.points.clear();
        out.points.reserve(src.points.size());
        for (const Vec2& point : src.points) {
            out.points.push_back({
                ((src.offset.x + point.x) - pivot.x) * frameW * absSx,
                ((src.offset.y + point.y) - pivot.y) * frameH * absSy,
            });
        }
    } else if (out.type == CollisionShapeType::Circle) {
        out.radius = std::max(0.5f, worldW * 0.5f);
    }
    return out;
}

std::vector<CollisionShape> convert_profile_shapes(
    const std::vector<CollisionShape>& profileShapes,
    CollisionProfileCoordinateSpace space,
    const SpriteAnimator* animator,
    EntityId entityId,
    const SpriteComponent& sprite,
    const Transform& transform)
{
    std::vector<CollisionShape> out;
    out.reserve(profileShapes.size());

    SpriteAnimator::Frame frame{};
    if (animator) {
        frame = animator->currentFrame(entityId);
        if (frame.w <= 0 || frame.h <= 0)
            frame = animator->firstFrameForAsset(sprite.spriteAssetId);
    }
    if (frame.w <= 0) frame.w = 32;
    if (frame.h <= 0) frame.h = 32;

    const Vec2 pivot = sprite.pivotFromAsset ? Vec2{0.5f, 0.5f} : sprite.pivot;

    for (const CollisionShape& src : profileShapes) {
        if (!src.enabled) continue;
        if (space == CollisionProfileCoordinateSpace::FrameNormalized) {
            out.push_back(shape_from_normalized(
                src,
                static_cast<float>(frame.w),
                static_cast<float>(frame.h),
                transform.scale.x,
                transform.scale.y,
                pivot));
        } else {
            out.push_back(src);
        }
    }
    return out;
}

} // namespace

bool resolve_collision_body(
    EntityId entityId,
    const SpriteComponent& sprite,
    const Transform& transform,
    const CollisionBodyComponent& authored,
    const std::unordered_map<std::string, CollisionProfileDef>& profiles,
    const std::unordered_map<std::string, std::string>& spritePathToAssetId,
    const SpriteAnimator* animator,
    CollisionBodyComponent& out)
{
    out = authored;
    if (!out.enabled) return false;

    const CollisionProfileDef* profile = find_profile(
        authored, sprite, profiles, spritePathToAssetId);
    if (profile && !profile->shapes.empty()) {
        out.shapes = convert_profile_shapes(
            profile->shapes, profile->coordinateSpace, animator,
            entityId, sprite, transform);
        return !out.shapes.empty();
    }

    return !out.shapes.empty();
}

} // namespace ArtCade::Modules::CollisionProfileResolve
