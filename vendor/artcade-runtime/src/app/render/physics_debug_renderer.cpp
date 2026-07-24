#include "physics_debug_renderer.h"

#include "../../modules/collision/include/collision_math.h"
#include "../../modules/collision/include/collision_world.h"
#include "../../modules/renderer/include/renderer.h"
#include "../../world/include/world.h"

#include <cmath>
#include <string>

namespace ArtCade::AppRender {

namespace {

constexpr float kPi = 3.14159265358979323846f;

Vec4 roleColor(CollisionShapeRole role, CollisionResponse response) {
    Vec4 color{0.22f, 0.74f, 0.98f, 0.9f};
    switch (role) {
    case CollisionShapeRole::Feet: color = {0.2f, 0.9f, 0.35f, 0.9f}; break;
    case CollisionShapeRole::Hurtbox: color = {1.f, 0.5f, 0.1f, 0.9f}; break;
    case CollisionShapeRole::Hitbox: color = {1.f, 0.18f, 0.18f, 0.9f}; break;
    case CollisionShapeRole::Interaction: color = {0.1f, 0.86f, 0.78f, 0.9f}; break;
    case CollisionShapeRole::Body:
    default: break;
    }
    if (response == CollisionResponse::Sensor)
        color.a = 0.62f;
    return color;
}

void drawRectOutline(Modules::Renderer& renderer,
                     const PhysicsMath::Aabb& box,
                     const Vec4& color) {
    renderer.drawLine(box.minX, box.minY, box.maxX, box.minY, color);
    renderer.drawLine(box.maxX, box.minY, box.maxX, box.maxY, color);
    renderer.drawLine(box.maxX, box.maxY, box.minX, box.maxY, color);
    renderer.drawLine(box.minX, box.maxY, box.minX, box.minY, color);
}

void drawCircleOutline(Modules::Renderer& renderer,
                       Vec2 center,
                       float radius,
                       const Vec4& color) {
    constexpr int segments = 32;
    Vec2 prev{center.x + radius, center.y};
    for (int i = 1; i <= segments; ++i) {
        const float t = (static_cast<float>(i) / segments) * kPi * 2.f;
        Vec2 next{center.x + std::cos(t) * radius, center.y + std::sin(t) * radius};
        renderer.drawLine(prev.x, prev.y, next.x, next.y, color);
        prev = next;
    }
}

void drawPolygonOutline(Modules::Renderer& renderer,
                        const std::vector<Vec2>& points,
                        const Vec4& color) {
    if (points.size() < 2) return;
    for (size_t i = 0; i < points.size(); ++i) {
        const Vec2 a = points[i];
        const Vec2 b = points[(i + 1) % points.size()];
        renderer.drawLine(a.x, a.y, b.x, b.y, color);
    }
}

void drawCapsuleOutline(Modules::Renderer& renderer,
                        const PhysicsMath::ShapeInstance& instance,
                        const Vec4& color) {
    const PhysicsMath::CapsuleSegment capsule = PhysicsMath::capsuleSegment(instance);
    drawCircleOutline(renderer, capsule.a, capsule.radius, color);
    drawCircleOutline(renderer, capsule.b, capsule.radius, color);
    if (std::abs(capsule.a.x - capsule.b.x) < 0.01f) {
        renderer.drawLine(
            capsule.a.x - capsule.radius, capsule.a.y,
            capsule.b.x - capsule.radius, capsule.b.y,
            color);
        renderer.drawLine(
            capsule.a.x + capsule.radius, capsule.a.y,
            capsule.b.x + capsule.radius, capsule.b.y,
            color);
    } else {
        renderer.drawLine(
            capsule.a.x, capsule.a.y - capsule.radius,
            capsule.b.x, capsule.b.y - capsule.radius,
            color);
        renderer.drawLine(
            capsule.a.x, capsule.a.y + capsule.radius,
            capsule.b.x, capsule.b.y + capsule.radius,
            color);
    }
}

void drawShape(Modules::Renderer& renderer,
               const CollisionWorld::ShapeRef& shape) {
    const Vec4 color = roleColor(shape.shape.role, shape.shape.response);
    if (shape.instance.shape == ColliderShape::Circle) {
        drawCircleOutline(
            renderer,
            PhysicsMath::shapeCenter(shape.instance),
            shape.instance.size.x,
            color);
    } else if (shape.instance.shape == ColliderShape::Capsule) {
        drawCapsuleOutline(renderer, shape.instance, color);
    } else if (shape.instance.shape == ColliderShape::Polygon) {
        drawPolygonOutline(
            renderer,
            PhysicsMath::polygonWorldPoints(shape.instance),
            color);
    } else {
        drawRectOutline(renderer, shape.aabb, color);
    }

    const std::string label =
        CollisionWorld::roleName(shape.shape.role) + "/"
        + CollisionWorld::responseName(shape.shape.response) + " "
        + shape.shape.layerId;
    renderer.drawText(label, shape.aabb.minX, shape.aabb.minY - 12.f, 10, color);
}

} // namespace

void drawCollisionDebug(Modules::Renderer& renderer,
                        const World& world) {
    for (const auto& shape : world.collisionShapes())
        drawShape(renderer, shape);

    const Vec4 normalColor{1.f, 1.f, 0.2f, 0.95f};
    for (const auto& event : world.collisionEvents()) {
        if (event.kind == CollisionWorld::ContactEvent::Kind::Exit)
            continue;
        renderer.drawLine(
            event.point.x,
            event.point.y,
            event.point.x + event.normal.x * 18.f,
            event.point.y + event.normal.y * 18.f,
            normalColor);
    }
}

} // namespace ArtCade::AppRender
