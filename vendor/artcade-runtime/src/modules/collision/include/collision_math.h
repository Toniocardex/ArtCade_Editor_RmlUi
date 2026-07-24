#pragma once

// collision_math.h — Stateless collision algebra (shared World + Physics).
// Screen-space Y-down; axis-aligned shapes only (rotation 0).

#include "../../../core/types.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ArtCade::PhysicsMath {

struct Aabb {
    float minX = 0.f;
    float minY = 0.f;
    float maxX = 0.f;
    float maxY = 0.f;
};

struct ShapeInstance {
    ColliderShape shape = ColliderShape::Rectangle;
    Vec2            position{};
    Vec2            offset{};
    Vec2            size{ 1.f, 1.f }; // rect: full w/h; circle: radius in x
    std::vector<Vec2> points;         // polygon: local points from position+offset
};

inline Aabb aabbFromRect(const Vec2& center, float halfW, float halfH) {
    return {
        center.x - halfW,
        center.y - halfH,
        center.x + halfW,
        center.y + halfH,
    };
}

inline Vec2 shapeCenter(const ShapeInstance& s) {
    return { s.position.x + s.offset.x, s.position.y + s.offset.y };
}

inline Aabb shapeWorldAabb(const ShapeInstance& s) {
    const Vec2 c = shapeCenter(s);
    if (s.shape == ColliderShape::Circle) {
        const float r = s.size.x;
        return aabbFromRect(c, r, r);
    }
    if (s.shape == ColliderShape::Polygon && !s.points.empty()) {
        Aabb box{
            c.x + s.points[0].x,
            c.y + s.points[0].y,
            c.x + s.points[0].x,
            c.y + s.points[0].y,
        };
        for (const Vec2& p : s.points) {
            const float x = c.x + p.x;
            const float y = c.y + p.y;
            box.minX = std::min(box.minX, x);
            box.minY = std::min(box.minY, y);
            box.maxX = std::max(box.maxX, x);
            box.maxY = std::max(box.maxY, y);
        }
        return box;
    }
    return aabbFromRect(c, s.size.x * 0.5f, s.size.y * 0.5f);
}

/** Inclusive edges — touching counts as overlap (physics queries). */
inline bool aabbOverlap(const Aabb& a, const Aabb& b) {
    return a.minX <= b.maxX && a.maxX >= b.minX
        && a.minY <= b.maxY && a.maxY >= b.minY;
}

inline bool horizontalOverlap(const Aabb& a, const Aabb& b) {
    return a.minX <= b.maxX && a.maxX >= b.minX;
}

/** Platformer solid resolve: inclusive X, exclusive Y (edge contact). */
inline bool aabbOverlapPlatformer(const Aabb& a, const Aabb& b) {
    return horizontalOverlap(a, b)
        && a.maxY > b.minY && a.minY < b.maxY;
}

inline bool verticalOverlap(const Aabb& a, const Aabb& b) {
    return a.maxY > b.minY && a.minY < b.maxY;
}

inline bool circleCircleOverlap(const Vec2& c1, float r1, const Vec2& c2, float r2) {
    const float dx = c2.x - c1.x;
    const float dy = c2.y - c1.y;
    const float r  = r1 + r2;
    return dx * dx + dy * dy <= r * r;
}

inline bool circleRectOverlap(const Vec2& center, float radius, const Aabb& rect) {
    const float cx = std::clamp(center.x, rect.minX, rect.maxX);
    const float cy = std::clamp(center.y, rect.minY, rect.maxY);
    const float dx = center.x - cx;
    const float dy = center.y - cy;
    return dx * dx + dy * dy <= radius * radius;
}

inline float dot(Vec2 a, Vec2 b) {
    return a.x * b.x + a.y * b.y;
}

inline Vec2 subtract(Vec2 a, Vec2 b) {
    return { a.x - b.x, a.y - b.y };
}

inline float lengthSq(Vec2 v) {
    return dot(v, v);
}

inline Vec2 closestPointOnSegment(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 ab = subtract(b, a);
    const float denom = lengthSq(ab);
    if (denom < 1e-8f) return a;
    const float t = std::clamp(dot(subtract(p, a), ab) / denom, 0.f, 1.f);
    return { a.x + ab.x * t, a.y + ab.y * t };
}

inline float distancePointSegmentSq(Vec2 p, Vec2 a, Vec2 b) {
    const Vec2 closest = closestPointOnSegment(p, a, b);
    return lengthSq(subtract(p, closest));
}

inline float orientation(Vec2 a, Vec2 b, Vec2 c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

inline bool onSegment(Vec2 a, Vec2 b, Vec2 p) {
    return p.x >= std::min(a.x, b.x) - 1e-5f
        && p.x <= std::max(a.x, b.x) + 1e-5f
        && p.y >= std::min(a.y, b.y) - 1e-5f
        && p.y <= std::max(a.y, b.y) + 1e-5f;
}

inline bool segmentsIntersect(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    const float o1 = orientation(a, b, c);
    const float o2 = orientation(a, b, d);
    const float o3 = orientation(c, d, a);
    const float o4 = orientation(c, d, b);
    if (std::abs(o1) <= 1e-5f && onSegment(a, b, c)) return true;
    if (std::abs(o2) <= 1e-5f && onSegment(a, b, d)) return true;
    if (std::abs(o3) <= 1e-5f && onSegment(c, d, a)) return true;
    if (std::abs(o4) <= 1e-5f && onSegment(c, d, b)) return true;
    return ((o1 < 0.f) != (o2 < 0.f))
        && ((o3 < 0.f) != (o4 < 0.f));
}

inline float distanceSegmentSegmentSq(Vec2 a, Vec2 b, Vec2 c, Vec2 d) {
    if (segmentsIntersect(a, b, c, d)) return 0.f;
    return std::min({
        distancePointSegmentSq(a, c, d),
        distancePointSegmentSq(b, c, d),
        distancePointSegmentSq(c, a, b),
        distancePointSegmentSq(d, a, b),
    });
}

inline std::vector<Vec2> rectWorldPoints(const Aabb& box) {
    return {
        { box.minX, box.minY },
        { box.maxX, box.minY },
        { box.maxX, box.maxY },
        { box.minX, box.maxY },
    };
}

inline std::vector<Vec2> polygonWorldPoints(const ShapeInstance& s) {
    if (s.shape == ColliderShape::Polygon && s.points.size() >= 3) {
        const Vec2 origin = shapeCenter(s);
        std::vector<Vec2> out;
        out.reserve(s.points.size());
        for (const Vec2& p : s.points)
            out.push_back({ origin.x + p.x, origin.y + p.y });
        return out;
    }
    return rectWorldPoints(shapeWorldAabb(s));
}

inline bool pointInConvexPolygon(Vec2 p, const std::vector<Vec2>& poly) {
    if (poly.size() < 3) return false;
    bool hasPositive = false;
    bool hasNegative = false;
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i];
        const Vec2 b = poly[(i + 1) % poly.size()];
        const float cross = orientation(a, b, p);
        hasPositive = hasPositive || cross > 1e-5f;
        hasNegative = hasNegative || cross < -1e-5f;
        if (hasPositive && hasNegative) return false;
    }
    return true;
}

inline void projectPolygon(const std::vector<Vec2>& poly,
                           Vec2 axis,
                           float& min_out,
                           float& max_out) {
    min_out = dot(poly[0], axis);
    max_out = min_out;
    for (size_t i = 1; i < poly.size(); ++i) {
        const float value = dot(poly[i], axis);
        min_out = std::min(min_out, value);
        max_out = std::max(max_out, value);
    }
}

inline bool separatedOnPolygonAxes(const std::vector<Vec2>& a,
                                   const std::vector<Vec2>& b) {
    for (size_t i = 0; i < a.size(); ++i) {
        const Vec2 p1 = a[i];
        const Vec2 p2 = a[(i + 1) % a.size()];
        const Vec2 edge = subtract(p2, p1);
        const Vec2 axis{ -edge.y, edge.x };
        if (lengthSq(axis) < 1e-8f) continue;

        float minA = 0.f;
        float maxA = 0.f;
        float minB = 0.f;
        float maxB = 0.f;
        projectPolygon(a, axis, minA, maxA);
        projectPolygon(b, axis, minB, maxB);
        if (maxA < minB || maxB < minA)
            return true;
    }
    return false;
}

inline bool polygonPolygonOverlap(const std::vector<Vec2>& a,
                                  const std::vector<Vec2>& b) {
    if (a.size() < 3 || b.size() < 3) return false;
    return !separatedOnPolygonAxes(a, b) && !separatedOnPolygonAxes(b, a);
}

inline float distancePointPolygonSq(Vec2 p, const std::vector<Vec2>& poly) {
    if (pointInConvexPolygon(p, poly)) return 0.f;
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 a = poly[i];
        const Vec2 b = poly[(i + 1) % poly.size()];
        best = std::min(best, distancePointSegmentSq(p, a, b));
    }
    return best;
}

inline float distanceSegmentPolygonSq(Vec2 a, Vec2 b, const std::vector<Vec2>& poly) {
    if (pointInConvexPolygon(a, poly) || pointInConvexPolygon(b, poly))
        return 0.f;
    float best = std::numeric_limits<float>::max();
    for (size_t i = 0; i < poly.size(); ++i) {
        const Vec2 c = poly[i];
        const Vec2 d = poly[(i + 1) % poly.size()];
        best = std::min(best, distanceSegmentSegmentSq(a, b, c, d));
    }
    return best;
}

inline bool circlePolygonOverlap(Vec2 center,
                                 float radius,
                                 const std::vector<Vec2>& poly) {
    return distancePointPolygonSq(center, poly) <= radius * radius;
}

struct CapsuleSegment {
    Vec2 a{};
    Vec2 b{};
    float radius = 0.5f;
};

inline CapsuleSegment capsuleSegment(const ShapeInstance& s) {
    const Vec2 c = shapeCenter(s);
    const float w = std::max(1.f, s.size.x);
    const float h = std::max(1.f, s.size.y);
    CapsuleSegment capsule;
    if (h >= w) {
        capsule.radius = w * 0.5f;
        const float halfSegment = std::max(0.f, h * 0.5f - capsule.radius);
        capsule.a = { c.x, c.y - halfSegment };
        capsule.b = { c.x, c.y + halfSegment };
    } else {
        capsule.radius = h * 0.5f;
        const float halfSegment = std::max(0.f, w * 0.5f - capsule.radius);
        capsule.a = { c.x - halfSegment, c.y };
        capsule.b = { c.x + halfSegment, c.y };
    }
    return capsule;
}

inline bool capsuleCircleOverlap(const ShapeInstance& capsuleShape,
                                 const ShapeInstance& circleShape) {
    const CapsuleSegment capsule = capsuleSegment(capsuleShape);
    const Vec2 circle = shapeCenter(circleShape);
    const float radius = capsule.radius + circleShape.size.x;
    return distancePointSegmentSq(circle, capsule.a, capsule.b) <= radius * radius;
}

inline bool capsulePolygonOverlap(const ShapeInstance& capsuleShape,
                                  const std::vector<Vec2>& poly) {
    const CapsuleSegment capsule = capsuleSegment(capsuleShape);
    return distanceSegmentPolygonSq(capsule.a, capsule.b, poly)
        <= capsule.radius * capsule.radius;
}

inline bool capsuleCapsuleOverlap(const ShapeInstance& a, const ShapeInstance& b) {
    const CapsuleSegment ca = capsuleSegment(a);
    const CapsuleSegment cb = capsuleSegment(b);
    const float radius = ca.radius + cb.radius;
    return distanceSegmentSegmentSq(ca.a, ca.b, cb.a, cb.b) <= radius * radius;
}

inline bool shapesOverlap(const ShapeInstance& a, const ShapeInstance& b) {
    const Vec2 ca = shapeCenter(a);
    const Vec2 cb = shapeCenter(b);

    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Circle)
        return circleCircleOverlap(ca, a.size.x, cb, b.size.x);

    if (a.shape == ColliderShape::Capsule && b.shape == ColliderShape::Capsule)
        return capsuleCapsuleOverlap(a, b);
    if (a.shape == ColliderShape::Capsule && b.shape == ColliderShape::Circle)
        return capsuleCircleOverlap(a, b);
    if (a.shape == ColliderShape::Circle && b.shape == ColliderShape::Capsule)
        return capsuleCircleOverlap(b, a);

    if (a.shape == ColliderShape::Capsule)
        return capsulePolygonOverlap(a, polygonWorldPoints(b));
    if (b.shape == ColliderShape::Capsule)
        return capsulePolygonOverlap(b, polygonWorldPoints(a));

    const Aabb aa = shapeWorldAabb(a);
    const Aabb ab = shapeWorldAabb(b);

    if (a.shape == ColliderShape::Rectangle && b.shape == ColliderShape::Rectangle)
        return aabbOverlap(aa, ab);

    if (a.shape == ColliderShape::Circle)
        return circlePolygonOverlap(ca, a.size.x, polygonWorldPoints(b));
    if (b.shape == ColliderShape::Circle)
        return circlePolygonOverlap(cb, b.size.x, polygonWorldPoints(a));

    return polygonPolygonOverlap(polygonWorldPoints(a), polygonWorldPoints(b));
}

/** Minimum translation to separate movable AABB from fixed AABB (Y-down). */
inline bool resolveAabbSeparation(Aabb& movable, const Aabb& fixed, Vec2& outCorrection) {
    if (!aabbOverlap(movable, fixed)) return false;

    const float penLeft  = movable.maxX - fixed.minX;
    const float penRight = fixed.maxX - movable.minX;
    const float penUp    = movable.maxY - fixed.minY;
    const float penDown  = fixed.maxY - movable.minY;

    const float penX = std::min(penLeft, penRight);
    const float penY = std::min(penUp, penDown);

    outCorrection = {};
    if (penX < penY) {
        const float movableCx = (movable.minX + movable.maxX) * 0.5f;
        const float fixedCx   = (fixed.minX + fixed.maxX) * 0.5f;
        outCorrection.x = (movableCx < fixedCx) ? -penX : penX;
    } else {
        const float movableCy = (movable.minY + movable.maxY) * 0.5f;
        const float fixedCy   = (fixed.minY + fixed.maxY) * 0.5f;
        outCorrection.y = (movableCy < fixedCy) ? -penY : penY;
    }
    return true;
}

inline void translateAabb(Aabb& box, const Vec2& delta) {
    box.minX += delta.x;
    box.maxX += delta.x;
    box.minY += delta.y;
    box.maxY += delta.y;
}

struct RaycastHit {
    bool  hit      = false;
    float fraction = 1.f;
    Vec2  point{};
};

/** Result of sweeping a moving shape against a fixed shape over one delta. */
struct SweepHit {
    bool  hit = false;
    float fraction = 1.f;
    Vec2  normal{};
    Vec2  point{};
};

/** Segment vs axis-aligned box; returns closest hit with fraction in [0,1]. */
inline RaycastHit raycastSegmentVsAabb(const Vec2& from, const Vec2& to, const Aabb& box) {
    RaycastHit best;
    const Vec2 dir  = { to.x - from.x, to.y - from.y };
    const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
    if (len < 1e-6f) {
        const bool inside = from.x >= box.minX && from.x <= box.maxX
                         && from.y >= box.minY && from.y <= box.maxY;
        if (inside) {
            best.hit      = true;
            best.fraction = 0.f;
            best.point    = from;
        }
        return best;
    }

    float tMin = 0.f;
    float tMax = 1.f;

    const float invDx = std::abs(dir.x) > 1e-8f ? 1.f / dir.x : 0.f;
    const float invDy = std::abs(dir.y) > 1e-8f ? 1.f / dir.y : 0.f;

    if (std::abs(dir.x) > 1e-8f) {
        float t1 = (box.minX - from.x) * invDx;
        float t2 = (box.maxX - from.x) * invDx;
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
    } else if (from.x < box.minX || from.x > box.maxX) {
        return best;
    }

    if (std::abs(dir.y) > 1e-8f) {
        float t1 = (box.minY - from.y) * invDy;
        float t2 = (box.maxY - from.y) * invDy;
        if (t1 > t2) std::swap(t1, t2);
        tMin = std::max(tMin, t1);
        tMax = std::min(tMax, t2);
    } else if (from.y < box.minY || from.y > box.maxY) {
        return best;
    }

    if (tMax >= tMin && tMax >= 0.f && tMin <= 1.f) {
        const float t = std::clamp(tMin, 0.f, 1.f);
        best.hit      = true;
        best.fraction = t;
        best.point    = { from.x + dir.x * t, from.y + dir.y * t };
    }
    return best;
}

inline RaycastHit raycastSegmentVsCircle(const Vec2& from, const Vec2& to,
                                         const Vec2& center, float radius) {
    RaycastHit best;
    const Vec2  d  = { to.x - from.x, to.y - from.y };
    const Vec2  f  = { from.x - center.x, from.y - center.y };
    const float a  = d.x * d.x + d.y * d.y;
    if (a < 1e-8f) {
        const float distSq = f.x * f.x + f.y * f.y;
        if (distSq <= radius * radius) {
            best.hit      = true;
            best.fraction = 0.f;
            best.point    = from;
        }
        return best;
    }
    const float b  = 2.f * (f.x * d.x + f.y * d.y);
    const float c  = f.x * f.x + f.y * f.y - radius * radius;
    float       disc = b * b - 4.f * a * c;
    if (disc < 0.f) return best;
    disc = std::sqrt(disc);
    const float t1 = (-b - disc) / (2.f * a);
    const float t2 = (-b + disc) / (2.f * a);
    float tHit = std::numeric_limits<float>::max();
    if (t1 >= 0.f && t1 <= 1.f) tHit = std::min(tHit, t1);
    if (t2 >= 0.f && t2 <= 1.f) tHit = std::min(tHit, t2);
    if (tHit <= 1.f) {
        best.hit      = true;
        best.fraction = tHit;
        best.point    = { from.x + d.x * tHit, from.y + d.y * tHit };
    }
    return best;
}

/**
 * Sweeps a moving AABB across delta against a fixed AABB.
 * @param moving  starting world-space AABB.
 * @param delta   movement vector over the tested step.
 * @param fixed   stationary world-space AABB.
 * @returns earliest blocking hit; tangent edge contact is not blocking.
 */
inline SweepHit sweepAabb(const Aabb& moving, const Vec2& delta, const Aabb& fixed) {
    SweepHit hit;
    const bool penetrating = moving.minX < fixed.maxX && moving.maxX > fixed.minX
        && moving.minY < fixed.maxY && moving.maxY > fixed.minY;
    if (penetrating) {
        hit.hit = true;
        hit.fraction = 0.f;
        hit.point = {
            (std::max(moving.minX, fixed.minX) + std::min(moving.maxX, fixed.maxX)) * 0.5f,
            (std::max(moving.minY, fixed.minY) + std::min(moving.maxY, fixed.maxY)) * 0.5f,
        };
        return hit;
    }

    const Vec2 movingCenter{
        (moving.minX + moving.maxX) * 0.5f,
        (moving.minY + moving.maxY) * 0.5f,
    };
    const float movingHalfW = (moving.maxX - moving.minX) * 0.5f;
    const float movingHalfH = (moving.maxY - moving.minY) * 0.5f;
    const Aabb expanded{
        fixed.minX - movingHalfW,
        fixed.minY - movingHalfH,
        fixed.maxX + movingHalfW,
        fixed.maxY + movingHalfH,
    };
    const Vec2 target{
        movingCenter.x + delta.x,
        movingCenter.y + delta.y,
    };
    const RaycastHit ray = raycastSegmentVsAabb(movingCenter, target, expanded);
    if (!ray.hit)
        return hit;

    hit.hit = true;
    hit.fraction = ray.fraction;
    hit.point = ray.point;
    const float eps = 1e-5f;
    if (std::abs(ray.point.x - expanded.minX) <= eps)
        hit.normal = { -1.f, 0.f };
    else if (std::abs(ray.point.x - expanded.maxX) <= eps)
        hit.normal = { 1.f, 0.f };
    else if (std::abs(ray.point.y - expanded.minY) <= eps)
        hit.normal = { 0.f, -1.f };
    else if (std::abs(ray.point.y - expanded.maxY) <= eps)
        hit.normal = { 0.f, 1.f };
    if (dot(delta, hit.normal) >= -1e-6f)
        return {};
    return hit;
}

inline bool segmentIntersectionFraction(Vec2 from,
                                        Vec2 to,
                                        Vec2 a,
                                        Vec2 b,
                                        float& fraction) {
    const Vec2 r = subtract(to, from);
    const Vec2 s = subtract(b, a);
    const float denom = r.x * s.y - r.y * s.x;
    if (std::abs(denom) < 1e-8f)
        return false;
    const Vec2 diff = subtract(a, from);
    const float t = (diff.x * s.y - diff.y * s.x) / denom;
    const float u = (diff.x * r.y - diff.y * r.x) / denom;
    if (t < 0.f || t > 1.f || u < 0.f || u > 1.f)
        return false;
    fraction = t;
    return true;
}

inline RaycastHit raycastSegmentVsPolygon(const Vec2& from,
                                          const Vec2& to,
                                          const std::vector<Vec2>& poly) {
    RaycastHit best;
    if (poly.size() < 3) return best;
    if (pointInConvexPolygon(from, poly)) {
        best.hit = true;
        best.fraction = 0.f;
        best.point = from;
        return best;
    }

    for (size_t i = 0; i < poly.size(); ++i) {
        float fraction = 1.f;
        if (!segmentIntersectionFraction(
                from, to, poly[i], poly[(i + 1) % poly.size()], fraction))
            continue;
        if (fraction >= best.fraction)
            continue;
        best.hit = true;
        best.fraction = fraction;
        best.point = {
            from.x + (to.x - from.x) * fraction,
            from.y + (to.y - from.y) * fraction,
        };
    }
    return best;
}

inline RaycastHit nearerHit(RaycastHit best, RaycastHit candidate) {
    if (!candidate.hit || candidate.fraction >= best.fraction)
        return best;
    return candidate;
}

inline RaycastHit raycastSegmentVsCapsule(const Vec2& from,
                                          const Vec2& to,
                                          const ShapeInstance& shape) {
    const CapsuleSegment capsule = capsuleSegment(shape);
    RaycastHit best = raycastSegmentVsCircle(from, to, capsule.a, capsule.radius);
    best = nearerHit(best, raycastSegmentVsCircle(from, to, capsule.b, capsule.radius));

    Aabb body{};
    if (std::abs(capsule.a.x - capsule.b.x) < 1e-6f) {
        body = {
            capsule.a.x - capsule.radius,
            std::min(capsule.a.y, capsule.b.y),
            capsule.a.x + capsule.radius,
            std::max(capsule.a.y, capsule.b.y),
        };
    } else {
        body = {
            std::min(capsule.a.x, capsule.b.x),
            capsule.a.y - capsule.radius,
            std::max(capsule.a.x, capsule.b.x),
            capsule.a.y + capsule.radius,
        };
    }
    return nearerHit(best, raycastSegmentVsAabb(from, to, body));
}

inline RaycastHit raycastSegmentVsShape(const Vec2& from, const Vec2& to,
                                        const ShapeInstance& shape) {
    if (shape.shape == ColliderShape::Circle)
        return raycastSegmentVsCircle(from, to, shapeCenter(shape), shape.size.x);
    if (shape.shape == ColliderShape::Capsule)
        return raycastSegmentVsCapsule(from, to, shape);
    if (shape.shape == ColliderShape::Polygon)
        return raycastSegmentVsPolygon(from, to, polygonWorldPoints(shape));
    return raycastSegmentVsAabb(from, to, shapeWorldAabb(shape));
}

inline bool pointInsideShape(const Vec2& point, const ShapeInstance& shape) {
    if (shape.shape == ColliderShape::Circle) {
        const Vec2 c = shapeCenter(shape);
        const float dx = point.x - c.x;
        const float dy = point.y - c.y;
        return dx * dx + dy * dy <= shape.size.x * shape.size.x;
    }
    if (shape.shape == ColliderShape::Capsule) {
        const CapsuleSegment capsule = capsuleSegment(shape);
        return distancePointSegmentSq(point, capsule.a, capsule.b)
            <= capsule.radius * capsule.radius;
    }
    if (shape.shape == ColliderShape::Polygon)
        return pointInConvexPolygon(point, polygonWorldPoints(shape));
    const Aabb box = shapeWorldAabb(shape);
    return point.x >= box.minX && point.x <= box.maxX
        && point.y >= box.minY && point.y <= box.maxY;
}

} // namespace ArtCade::PhysicsMath
